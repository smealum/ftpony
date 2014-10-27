#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <3ds.h>

#include "output.h"
#include "ftp.h"

FS_archive sdmcArchive;

char tmpBuffer[512];
const int commandPort=5000;
const int dataPort=5001;

#define DATA_BUFFER_SIZE (512*1024)

char currentPath[4096];
char tmpStr[4096];
u32 dataBuffer[DATA_BUFFER_SIZE/4];

void ftp_init()
{
	Result ret;
	ret=fsInit();
	print("fsInit %08X\n", (unsigned int)ret);

	sdmcArchive=(FS_archive){0x00000009, (FS_path){PATH_EMPTY, 1, (u8*)""}};
	FSUSER_OpenArchive(NULL, &sdmcArchive);
	print("FSUSER_OpenArchive %08X\n", (unsigned int)ret);

	ret=SOC_Initialize((u32*)0x8bae000, 0x100000);
	print("SOC_Initialize %08X\n", (unsigned int)ret);

	sprintf(currentPath, "/");
}

void ftp_exit()
{
	SOC_Shutdown();
}

unsigned long htonl(unsigned long v)
{
	u8* v2=(u8*)&v;
	return (v2[0]<<24)|(v2[1]<<16)|(v2[2]<<8)|(v2[3]);
}

unsigned short htons(unsigned short v)
{
	u8* v2=(u8*)&v;
	return (v2[0]<<8)|(v2[1]);
}

int ftp_openCommandChannel()
{
	int listenfd;
	struct sockaddr_in serv_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(commandPort); 

	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

	listen(listenfd, 10); 
	
	int ret=accept(listenfd, (struct sockaddr*)NULL, NULL);
	closesocket(listenfd);

	return ret;
}

int ftp_openDataChannel()
{
	int listenfd;
	struct sockaddr_in serv_addr;

	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(dataPort); 

	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

	listen(listenfd, 10); 
	
	int ret=accept(listenfd, (struct sockaddr*)NULL, NULL);
	closesocket(listenfd);

	return ret;
}

int ftp_sendResponse(int s, int n, char* mes)
{
	char data[128];
	sprintf(data, "%d %s\r\n", n, mes);
	return send(s,data,strlen(data)+1,0);
}

void unicodeToChar(char* dst, u16* src)
{
	if(!src || !dst)return;
	while(*src)*(dst++)=(*(src++))&0xFF;
	*dst=0x00;
}

int linelen(char* str)
{
	int i=0; while(*str && *str!='\n' && *str!='\r'){i++;str++;}
	*str=0x0;
	return i;
}

int ftp_processCommand(int s, char* data)
{
	if(!data)return -1;
	int n=linelen(data);
	char cmd[5];
	char arg[256]="";
	if(n>2 && (!data[3] || data[3]==' ' || data[3]=='\n' || data[3]=='\r')){memcpy(cmd,data,3);cmd[3]=0x0; if(n>3)memcpy(arg, &data[4], n-4);}
	else if(n>3 && (!data[4] || data[4]==' ' || data[4]=='\r' || data[4]=='\n')){memcpy(cmd,data,4);cmd[4]=0x0; if(n>4)memcpy(arg, &data[5], n-5);}
	else return -1;

	print("\nreceived command : %s (%s)",cmd,arg);

	if(!strcmp(cmd, "PWD")){
		siprintf(tmpStr, "\"%s\"",currentPath);
		ftp_sendResponse(s, 257, tmpStr);
	}else if(!strcmp(cmd, "PASV")){
		char response[32];
		sprintf(response, "Entering Passive Mode (192,168,0,17,%d,%d)",(dataPort-(dataPort%256))/256,dataPort%256);
		ftp_sendResponse(s, 200, response);
	}else if(!strcmp(cmd, "LIST")){
		int data_s=ftp_openDataChannel();
		ftp_sendResponse(s, 150, "opening ASCII data channel");

		//send LIST data
		Handle dirHandle;
		FS_path dirPath=FS_makePath(PATH_CHAR, currentPath);
		FSUSER_OpenDirectory(NULL, &dirHandle, sdmcArchive, dirPath);

		u32 entriesRead=0;
		do{
			u16 entryBuffer[512];
			char data[256];
			FSDIR_Read(dirHandle, &entriesRead, 1, (FS_dirent*)entryBuffer);
			if(!entriesRead)break;
			unicodeToChar(data, entryBuffer);
			siprintf((char*)entryBuffer, "%crwxrwxrwx   2 3DS        %d Feb  1  2009 %s\r\n",entryBuffer[0x21c/2]?'d':'-',entryBuffer[0x220/2]|(entryBuffer[0x222/2]<<16),data);
			send(data_s, entryBuffer, strlen((char*)entryBuffer), 0);
		}while(entriesRead>0);
		u8 endByte=0x0;
		send(data_s, &endByte, 1, 0);
		FSDIR_Close(dirHandle);

		closesocket(data_s);
		ftp_sendResponse(s, 226, "transfer complete");
	}else if(!strcmp(cmd, "STOR")){
		ftp_sendResponse(s, 150, "opening binary data channel");
		int data_s=ftp_openDataChannel();

		sprintf(tmpStr, "%s%s",currentPath,arg);
		Handle fileHandle;
		FSUSER_OpenFile(NULL, &fileHandle, sdmcArchive, FS_makePath(PATH_CHAR, tmpStr), FS_OPEN_WRITE|FS_OPEN_CREATE, 0);
		int ret;
		u32 totalSize=0;
		while((ret=recv(data_s, dataBuffer, DATA_BUFFER_SIZE, 0))>0){FSFILE_Write(fileHandle, (u32*)&ret, totalSize, (u32*)dataBuffer, ret, 0x10001);totalSize+=ret;}
		FSFILE_Close(fileHandle);

		closesocket(data_s);
		ftp_sendResponse(s, 226, "transfer complete");
	}else if(!strcmp(cmd, "RETR")){
		ftp_sendResponse(s, 150, "opening binary data channel");
		int data_s=ftp_openDataChannel();

		sprintf(tmpStr, "%s%s",currentPath,arg);
		print("\n%s",tmpStr);
		Handle fileHandle;
		FSUSER_OpenFile(NULL, &fileHandle, sdmcArchive, FS_makePath(PATH_CHAR, tmpStr), FS_OPEN_READ, 0);
		int ret;
		u32 readSize=0;
		u32 totalSize=0;
		do{
			ret=FSFILE_Read(fileHandle, (u32*)&readSize, totalSize, (u32*)dataBuffer, DATA_BUFFER_SIZE);
			if(ret || !readSize)break;
			ret=send(data_s, dataBuffer, readSize, 0);
			totalSize+=readSize;
		}while(readSize && ret>0);
		FSFILE_Close(fileHandle);

		closesocket(data_s);
		ftp_sendResponse(s, 226, "transfer complete");
	}else if(!strcmp(cmd, "USER")){
		ftp_sendResponse(s, 200, "password ?");
	}else if(!strcmp(cmd, "PASS")){
		ftp_sendResponse(s, 200, "ok");
	}else if(!strcmp(cmd, "CWD")){
		if(arg[0]=='/')strcpy(currentPath,arg);
		else strcat(currentPath,arg);
		strcat(currentPath,"/");
		ftp_sendResponse(s, 200, "ok");
	}else if(!strcmp(cmd, "RETR")){
		ftp_sendResponse(s, 200, "ok");
	}else if(!strcmp(cmd, "TYPE")){
		ftp_sendResponse(s, 200, "changed type");
	}else if(!strcmp(cmd, "QUIT")){
		ftp_sendResponse(s, 221, "disconnecting");
	}else{
		ftp_sendResponse(s, 502, "invalid command");
	}
	return 0;
}

int ftp_frame(int s)
{
	char buffer[512];
	memset(buffer, 0, 512);
	recv(s,buffer,512,0);
	return ftp_processCommand(s,buffer);
}

int ftp_getConnection()
{
	int connfd = ftp_openCommandChannel();
	print("received connection ! %d\ngreeting...",connfd);
	ftp_sendResponse(connfd, 200, "hello");
	return connfd;
}
