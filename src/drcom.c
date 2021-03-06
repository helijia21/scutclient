#include "drcom.h"
#include "functions.h"

typedef enum {REQUEST=1, RESPONSE=2, SUCCESS=3, FAILURE=4, H3CDATA=10} EAP_Code;
typedef enum {IDENTITY=1, NOTIFICATION=2, MD5=4, AVAILABLE=20, ALLOCATED=7} EAP_Type;
typedef enum {MISC_0800=0x08, ALIVE_FILE=0x10, MISC_3000=0x30, MISC_2800=0x28} DRCOM_Type;

static uint8_t crc_md5_info[16] = {0};
static int drcom_package_id = 0;  // 包的id，每次自增1
char drcom_misc1_flux[4];
char drcom_misc3_flux[4];


uint32_t checkCPULittleEndian()
{
    union
    {
        unsigned int a;
        unsigned char b;
    } c;
    c.a = 1;
    return (c.b == 1);
}

uint32_t big2little_32(uint32_t A)
{
    return ((((uint32_t)(A) & 0xff000000) >>24) | 
        (((uint32_t)(A) & 0x00ff0000) >> 8) | 
        (((uint32_t)(A) & 0x0000ff00) << 8) | 
        (((uint32_t)(A) & 0x000000ff) << 24));
}

uint32_t drcom_crc32(char *data, int data_len)
{
	uint32_t ret = 0;
	int i;
	for (i = 0; i < data_len;i += 4) 
	{
		ret ^= *(unsigned int *) (data + i);
		ret &= 0xFFFFFFFF;
	}

	// 大端小端的坑
	if(checkCPULittleEndian() == 0)
	{
		ret = big2little_32(ret);
	}
	ret = (ret * 19680126) & 0xFFFFFFFF;
	if(checkCPULittleEndian() == 0)
	{
		ret = big2little_32(ret);
	}

	return ret;
}

void encrypt(unsigned char *info)
{
	int i;
	unsigned char *chartmp= NULL;
	chartmp = (unsigned char *)malloc(16);
	for(i = 0 ; i < 16 ; i++)
	{
		chartmp[i] = (unsigned char)((info[i] << (i & 0x07)) + (info[i] >> (8-(i & 0x07))));
	}
	memcpy(info,chartmp,16);
	free(chartmp);
}

size_t AppendDrcomStartPkt( uint8_t EthHeader[], uint8_t *Packet )
{
	size_t packetlen = 0;
	memset(Packet, 0x00,97);//fill 0x00
	// Ethernet Header (14 Bytes)
	memcpy(Packet, EthHeader,14);
	// EAPOL (4 Bytes)
	Packet[14] = 0x01;	// Version=1
	Packet[15] = 0x01;	// Type=Start
	Packet[16] = 0x00;// Length=0x0000
	Packet[17] = 0x00;
	packetlen=96;

	PrintDebugInfo(	"Start", Packet, packetlen);

	return packetlen;
}

size_t AppendDrcomResponseIdentity(const uint8_t request[], uint8_t EthHeader[], unsigned char *UserName, uint8_t *Packet )
{
	size_t packetlen = 0;
	size_t userlen = strlen(UserName);
	memset(Packet, 0x00,97);//fill 0x00
	uint16_t eaplen;
	// Fill Ethernet header
	memcpy(Packet, EthHeader,14);
	// 802,1X Authentication
	Packet[14] = 0x1;	// 802.1X Version 1
	Packet[15] = 0x0;	// Type=0 (EAP Packet)
	//Packet[16~17]留空	// Length
	// Extensible Authentication Protocol
	Packet[18] = /*(EAP_Code)*/ RESPONSE;	// Code
	Packet[19] = request[19];		// ID
	//Packet[20~21]留空			// Length
	Packet[22] = /*(EAP_Type)*/ IDENTITY;	// Type
	//fill username and ip
	packetlen = 23;
	memcpy(Packet+packetlen, UserName, userlen);
	packetlen += userlen;
	Packet[packetlen++] = 0x0;
	Packet[packetlen++] = 0x44;
	Packet[packetlen++] = 0x61;
	Packet[packetlen++] = 0x0;
	Packet[packetlen++] = 0x0;
	uint8_t ip[4]= {0};
	GetWanIpFromDevice(ip);
	memcpy(Packet+packetlen, ip, 4);
	packetlen += 4;
	if(packetlen < 96)
	{
		packetlen = 96;
	}
	// 补填前面留空的两处Length
	eaplen = htons(userlen+14);
	memcpy(Packet+16, &eaplen, sizeof(eaplen));
	eaplen = htons(userlen+14);
	memcpy(Packet+20, &eaplen, sizeof(eaplen));
	
	PrintDebugInfo(	"Identity", Packet, packetlen);

	return packetlen;
}

size_t AppendDrcomResponseMD5(const uint8_t request[],uint8_t EthHeader[], unsigned char *UserName, unsigned char *Password, uint8_t *Packet)
{
	size_t packetlen = 0;
	size_t userlen = strlen(UserName);
	uint16_t eaplen = 0;
	memset(Packet, 0x00,97);//fill 0x00
	
	// Fill Ethernet header
	memcpy(Packet, EthHeader,14);

	// 802,1X Authentication
	Packet[14] = 0x1;	// 802.1X Version 1
	Packet[15] = 0x0;	// Type=0 (EAP Packet)
	//Packet[16~17]留空	// Length
	// Extensible Authentication Protocol
	Packet[18] = /*(EAP_Code)*/ RESPONSE;// Code
	Packet[19] = request[19];	// ID
	//Packet[20~21]留空	
	Packet[22] = /*(EAP_Type)*/ MD5;	// Type
	Packet[23] = 0x10;		// Value-Size: 16 Bytes
	packetlen = 24;
	FillMD5Area(Packet+packetlen, request[19], Password, request+24);
	// // 存好md5信息，以备后面udp报文使用
	// memcpy(crc_md5_info,Packet+packetlen,16);
	packetlen += 16;
	memcpy(Packet+packetlen, UserName, userlen);
	packetlen += userlen;
	Packet[packetlen++]= 0x0;
	Packet[packetlen++]= 0x44;
	Packet[packetlen++]= 0x61;
	Packet[packetlen++]= 0x2a;
	Packet[packetlen++]= 0x0;
	uint8_t ip[4]= {0};
	GetWanIpFromDevice(ip);
	memcpy(Packet+packetlen, ip, 4);  // 填充ip
	packetlen += 4;
	// 补填前面留空的两处Length
	eaplen = htons(userlen+31);
	memcpy(Packet+16, &eaplen, sizeof(eaplen));	// Length
	eaplen = htons(userlen+31);
	memcpy(Packet+20, &eaplen, sizeof(eaplen));	// Length

	if(packetlen < 96)
	{
		packetlen = 96;
	}

	PrintDebugInfo(	"MD5", Packet, packetlen);

	return packetlen;
}

size_t AppendDrcomLogoffPkt(uint8_t EthHeader[], uint8_t *Packet)
{
	size_t packetlen = 0;
	memset(Packet, 0xa5,97);//fill 0xa5
	// Ethernet Header (14 Bytes)
	memcpy(Packet, EthHeader,14);
	// EAPOL (4 Bytes)
	Packet[14] = 0x01;	// Version=1
	Packet[15] = 0x02;	// Type=Logoff
	Packet[16] = 0x00;// Length=0x0000
	Packet[17] = 0x00;
	packetlen=96;

	PrintDebugInfo(	"Logoff", Packet, packetlen);

	return packetlen;
}

int Drcom_MISC_START_ALIVE_Setter(unsigned char *send_data, char *recv_data)
{
	int packetlen = 0;
	send_data[packetlen++] = 0x07;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x08;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x01;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	return packetlen;
}

int Drcom_MISC_INFO_Setter(unsigned char *send_data, char *recv_data)
{
	int packetlen = 0;
	send_data[packetlen++] = 0x07;	// Code
	send_data[packetlen++] = 0x01;	//id
	send_data[packetlen++] = 0xf4;	//len(包的长度低位，一定要偶数长度的)
	send_data[packetlen++] = 0x00;	//len(244高位)
	send_data[packetlen++] = 0x03;	//step 第几步
	// 填用户名长度
	unsigned char username[32] = {0};
	GetUserName(username);
	send_data[packetlen++] = strlen(username); //uid len  用户ID长度
	// 填MAC
	uint8_t MAC[6]= {0};
	GetMacFromDevice(MAC);
	memcpy(send_data+packetlen, MAC, 6);
	packetlen += 6;
	// 填ip
	uint8_t ip[4]= {0};
	GetWanIpFromDevice(ip);
	memcpy(send_data+packetlen, ip, 4);
	packetlen += 4;
	
	send_data[packetlen++] = 0x02;
	send_data[packetlen++] = 0x22;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x2a;
	// 挑战码
	memcpy(send_data+packetlen, recv_data+8, 4);
	packetlen += 4;
	// crc32(后面再填)
	send_data[packetlen++] = 0xc7;
	send_data[packetlen++] = 0x2f;
	send_data[packetlen++] = 0x31;
	send_data[packetlen++] = 0x01;
	send_data[packetlen++] = 0x7e;// 做完crc32后，在把这个字节置位0
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	// 填用户名
	memcpy(send_data+packetlen, username, strlen(username));
	packetlen += strlen(username);
	// 填计算机名
	unsigned char hostname[32] = {0};
	GetHostNameFromDevice(hostname);
	memcpy(send_data+packetlen, hostname, 32 - strlen(username));
	packetlen += 32 - strlen(username);
	//填充32个0
	memset(send_data+packetlen,0x00,32);
	packetlen += 12;
	//填DNS
	uint8_t dns[4] = {0};
	GetWanDnsFromDevice(dns);
	memcpy(send_data+packetlen, dns, 4);
	packetlen += 4;
	// 第2,3个DNS忽略
	packetlen += 16;
	
	//0x0060 C10专有？？？
    send_data[packetlen++] = 0x94;
    send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
    send_data[packetlen++] = 0x06;
    send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
    send_data[packetlen++] = 0x02;
    send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
    send_data[packetlen++] = 0xf0;
    send_data[packetlen++] = 0x23;
    send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;

    //0x0070 C10专有？？？
    send_data[packetlen++] = 0x02;
    packetlen += 3;

	// 先填充64位0x00 (在这64位里面填充Drcom版本信息)
	memset(send_data+packetlen,0x00,64);
	// 填充Drcom版本信息
	unsigned char version[64] = {0};
	int len = GetVersionFromDevice(version);
	memcpy(send_data+packetlen, version, len);
	packetlen += 64;
	
	// 先填充68位0x00 (在这64位里面填充HASH信息，预留需要补0的四位)
	memset(send_data+packetlen,0x00,68);
	// 填充HASH信息
	unsigned char hash[64] = {0};
	GetHashFromDevice(hash);
	memcpy(send_data+packetlen, hash, strlen(hash));
	packetlen += 64;
	//判定是否是4的倍数
	if(packetlen % 4 != 0)
	{
		//补0，使包长度为4的倍数
		packetlen = packetlen + 4 - ( packetlen % 4 );
	}
	// 回填包的长度
	send_data[2]= 0xFF & packetlen;
	send_data[3]= 0xFF & (packetlen>>8);
	
	// 完成crc32校验
	uint32_t crc = drcom_crc32(send_data, packetlen);
	memcpy(send_data + 24, &crc, 4);
	//缓存crc32校验到crc_md5_info前4字节
	memcpy(crc_md5_info, &crc, 4);
	// 完成crc32校验，回填置位0
	send_data[28] = 0x00;
	return packetlen;
}

int Drcom_MISC_HEART_BEAT_01_TYPE_Setter(unsigned char *send_data, char *recv_data)
{	
	int packetlen = 0;
	memset(send_data, 0, 40);

	send_data[packetlen++] = 0x07;
	send_data[packetlen++] = drcom_package_id++;
	send_data[packetlen++] = 0x28;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x0b;
	send_data[packetlen++] = 0x01;
	send_data[packetlen++] = 0xdc;
	send_data[packetlen++] = 0x02;

	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	
	memcpy(send_data + 16, drcom_misc1_flux, 4);
	packetlen = 40;

	return packetlen;
}

int Drcom_MISC_HEART_BEAT_03_TYPE_Setter(unsigned char *send_data, char *recv_data)
{
	memcpy(&drcom_misc3_flux, recv_data + 16, 4);
	memset(send_data, 0, 40);

	int packetlen = 0;
	send_data[packetlen++] = 0x07;
	send_data[packetlen++] = drcom_package_id++;
	send_data[packetlen++] = 0x28;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x0b;
	send_data[packetlen++] = 0x03;
	send_data[packetlen++] = 0xdc;
	send_data[packetlen++] = 0x02;

	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	
	memcpy(send_data + 16, drcom_misc3_flux, 4);
	uint8_t ip[4]= {0};
	GetWanIpFromDevice(ip);
	memcpy(send_data + 28, ip, 4);
	packetlen = 40;

	return packetlen;
}

int Drcom_ALIVE_HEARTBEAT_TYPE_Setter(unsigned char *send_data, char *recv_data)
{
	int packetlen = 0;
	send_data[packetlen++] = 0xff;
	// 填充crc_md5_info信息
	memcpy(send_data+packetlen,crc_md5_info,16);
	packetlen += 16;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	send_data[packetlen++] = 0x00;
	
	//填充MISC_3000包解密得到的tail信息
	memcpy(send_data+packetlen,tailinfo,16);
	packetlen += 16;
	
	//时间信息
	uint16_t timeinfo = time(NULL);
	memcpy(send_data+packetlen,&timeinfo,2);
	packetlen += 2;
	return packetlen;
}

