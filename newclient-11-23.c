#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>  
#include <stdlib.h>  
#include <errno.h> 
#include <sys/ipc.h>  
#include <sys/stat.h>  
#include <sys/msg.h>
#include <string.h>

#define SERVER_PORT 8800 //server端接口
#define MY_PORT 8801//用户接入接口
#define PORT_4G 8802
#define buflen 4096
#define BACKLOG 20//多少等待控制 listen()
#define    MSGKEY   1024/*msgget()函数的第一个参数是消息队列对象的关键字(key)，函数将它与已有的消息队列对象的关键字进行比较来判断消息队列对象是否已经创建。*/
#define    AddrForServer  "127.0.0.1"
#define MessageTypeFor4G  100//4G消息的种类
#define MessageTypeForPPoe 101//PPoe  消息种类
#define true 1

struct MSGSTRUCT  
{  
    long    msgType;  
    char    msgText[buflen];  
};
struct sockaddr_in remote_addr;//服务器端网络地址结构体
struct sockaddr_in local_addr_ppoe;//本机网络地址结构体，监视端口用
struct sockaddr_in local_addr_4G;//4G卡网络地址结构体，监视端口用
int local_sockfd_ppoe;//客户端套接字描述符
int local_sockfd_4G;//4G卡客户的端口socket
int remote_sockfd_ppoe;//用来发送给server的套接字描述符
int remote_sockfd_4G;//4G用来发送给server的套接字描述符
int addrLen = sizeof(struct sockaddr_in);
char FeedbackForPPoe[buflen] = {'\0'};
char FeedbackFor4G[buflen] = {'\0'};
char RouterId[17];
int  RouterVersion = 0x0001;
char RouterFlag[2] = "RM";
int flagOfPop = 0;//出队是否出错
int flagOf4GEnQue = 0;
int flagOfPPoeEnQue = 0;

int MsgId; //消息队列对象的标识符

void threadOfGetInfoFromQue();
void threadOfPostInfoForPPoe();
void threadOfPostInfoFor4G();
int SendFeedbackToServer(char FeedbackInfo[]);
int getDataFromSocketInfo(char socketInfo[],char commandInfo[]);//0获取成功，1获取失败程序出错，-1不是本路由器的指令
void PrepareInfoForFeedback(char InfoHead[], char HeadOfData[], char DataInfo[], char FeedbackInfo[]);//0成功，1出错
int InfoEnQue(struct MSGSTRUCT *MsgInfoSnd);
int EnrollForRouter();


/*************main*********************************/
int main()
{
	int resOfCreateThread = 0;
	pthread_t threadOfPostFor4G,threadOfPostForPPoe,threadOfGetFromQue;
	int retOfEnroll = 0;//0注册成功，1注册失败.
	retOfEnroll = EnrollForRouter();
	if(retOfEnroll == 1)
	{
		perror("Router Enroll fail !\n");
		return 1;
	}
	/************建立消息队列*****************/
	MsgId = msgget(MSGKEY,IPC_CREAT|0666);//建立消息队列，返回队列ID
	MsgId = msgget(MSGKEY,IPC_EXCL);//判断消息队列是否存在
	if(MsgId < 0)
	{
		perror("create msgQ error in main!\n");
		return 1;
	}
	/**************remote_addr**服务器网络地址结构体*************************/
	memset(&remote_addr,0,sizeof(remote_addr)); //数据初始化--清零  
    remote_addr.sin_family=AF_INET; //设置为IP通信  
    remote_addr.sin_addr.s_addr=inet_addr(AddrForServer);//服务器IP地址  
    remote_addr.sin_port=htons(SERVER_PORT); //服务器端口号 
	
	/**************local_addr****ppoe客户端网络地址结构体***********************/
	memset(&local_addr_ppoe,0,sizeof(local_addr_ppoe));//初始化清零
	local_addr_ppoe.sin_family=AF_INET; //设置为IP通信,IPv4协议
	local_addr_ppoe.sin_addr.s_addr=inet_addr(INADDR_ANY);//本机IP地址
	local_addr_ppoe.sin_port=htons(MY_PORT); //本机端口号
	
	/**************addrFor4G******4G客户端网络地址结构体*********************/
	memset(&local_addr_4G,0,sizeof(local_addr_4G));//初始化清零
	local_addr_4G.sin_family=AF_INET; //设置为IP通信,IPv4协议
	local_addr_4G.sin_addr.s_addr=inet_addr(INADDR_ANY);//本机IP地址
	local_addr_4G.sin_port=htons(PORT_4G); //本机端口号
	
	/***************************ppoe消息入队线程******************************/
	resOfCreateThread = pthread_create(&threadOfPostFor4G,NULL,(void*) threadOfPostInfoFor4G,NULL);
	if(resOfCreateThread != 0)
	{
		perror("create 4G enque thread error !\n");
		return 1;
	}
	resOfCreateThread = pthread_create(&threadOfPostForPPoe,NULL,(void*) threadOfPostInfoForPPoe,NULL);
	if(resOfCreateThread != 0)
	{
		pthread_join(threadOfPostFor4G,NULL);
		perror("create ppoe enque thread error !\n");
		return 1;
	}
	resOfCreateThread = pthread_create(&threadOfGetFromQue,NULL,(void*) threadOfGetInfoFromQue,NULL);
	if(resOfCreateThread != 0)
	{
		pthread_join(threadOfPostFor4G,NULL);
		pthread_join(threadOfPostForPPoe,NULL);
		perror("create deque thread error !\n");
		return 1;
	}
	while(true)
	{
		if(flagOf4GEnQue == 1 || flagOfPPoeEnQue == 1 || flagOfPop == 1)
		{
			pthread_join(threadOfPostFor4G,NULL);
			pthread_join(threadOfPostForPPoe,NULL);
			pthread_join(threadOfGetFromQue,NULL);
			if(flagOf4GEnQue == 1)
			{
				perror("thread Of Post info for 4G error !\n");
				return 1;
			}
			if(flagOfPPoeEnQue == 1)
			{
				perror("thread Of Post info for PPoe error !\n" );
				return 1;
			}
			if(flagOfPop == 1)
			{
				perror("thread Of Get info from que error !\n");
				return 1;
			}
		}
	}
	return 0;
}
/******************************threadOfPostForPPoe()************************************/
void threadOfPostInfoForPPoe()
{
	int retOfbind = 0;
	int len = 0;//实际接收的socket信息的长度
	char buf[buflen] = {'\0'};

	int retOfEnque = 0;
	struct MSGSTRUCT MsgInfoSnd;
	
	local_sockfd_ppoe = socket(PF_INET,SERVER_PORT,0);
	if(local_sockfd_ppoe < 0)
	{
		perror("create socket of ppoe error !");
		flagOfPPoeEnQue = 1;
		return ;
	}
	/*将套接字绑定到服务器的网络地址上*/  
	retOfbind = bind(local_sockfd_ppoe, (struct sockaddr *)&local_addr_ppoe,sizeof(struct sockaddr));
	if(retOfbind < 0)
	{
		perror("socket  bind of ppoe error !\n");
		flagOfPPoeEnQue = 1;
		return ;
	}
	while(true)
	{
		memset(MsgInfoSnd.msgText,'\0',buflen);
		memset(FeedbackForPPoe,'\0',buflen);//每次将feedback初始化
		flagOfPPoeEnQue = 0;
		/*创建服务器端套接字--IPv4协议，面向连接通信，TCP协议*/ 
		
		/*监听连接请求--监听队列长度为BACKLOG*/  
		listen(local_sockfd_ppoe,BACKLOG);
		/*等待客户端连接请求到达*/  
		remote_sockfd_ppoe = accept(local_sockfd_ppoe,(struct sockaddr *)&remote_addr,&addrLen);
		if(remote_sockfd_ppoe < 0)
		{
			perror("ppoe socket accept error !\n");
			flagOfPPoeEnQue = 1;
			return ;
		}
		len = recv(remote_sockfd_ppoe ,buf, buflen, 0);//返回值为实际接收字符长度
		if(len == -1)
		{
			perror("client : ppoe socket recv error !\n");
			flagOfPPoeEnQue = 1;
			close(remote_sockfd_ppoe);
			return ;
		}
		strcpy(MsgInfoSnd.msgText,buf);
		MsgInfoSnd.msgType = MessageTypeForPPoe;
		retOfEnque = InfoEnQue(&MsgInfoSnd);
		if(retOfEnque == 1)
		{
			perror("msg EnQue for ppoe error !");
			flagOfPPoeEnQue = 1;
			close(remote_sockfd_ppoe);
			return ;
		}
		close(remote_sockfd_ppoe);	
	}	
}
/******************************threadOfPostFor4G()************************************/
void threadOfPostInfoFor4G()
{
	int retOfbind = 0;
	int len = 0;//实际接收的socket信息的长度
	char buf[buflen] = {'\0'};
	int retOfEnque = 0;
	struct MSGSTRUCT MsgInfoSnd;
	/*创建服务器端套接字--IPv4协议，面向连接通信，TCP协议*/ 
	local_sockfd_4G = socket(PF_INET,SERVER_PORT,0);
	if(local_sockfd_4G < 0)
	{
		perror("create socket of 4G error !\n");
		flagOf4GEnQue = 1;
		return ;
	}
	/*将套接字绑定到服务器的网络地址上*/  
	retOfbind = bind(local_sockfd_4G, (struct sockaddr *)&local_addr_4G,sizeof(struct sockaddr));
	if(retOfbind < 0)
	{
		perror("socket  bind of 4G error !\n");
		flagOf4GEnQue = 1;
		return ;
	}
	while(true)
	{
		memset(MsgInfoSnd.msgText,'\0',buflen);
		memset(FeedbackFor4G,'\0',buflen);//每次将feedback初始化
		flagOf4GEnQue = 0;
		
		/*监听连接请求--监听队列长度为BACKLOG*/  
		listen(local_sockfd_4G,BACKLOG);
		/*等待客户端连接请求到达*/  
		remote_sockfd_4G = accept(local_sockfd_4G,(struct sockaddr *)&remote_addr,&addrLen);
		if(remote_sockfd_4G < 0)
		{
			perror("4G socket accept error !\n");
			flagOf4GEnQue = 1;
			return ;
		}
		len = recv(remote_sockfd_4G ,buf, buflen, 0);//返回值为实际接收字符长度
		if(len == -1)
		{
			perror("client : 4G socket recv error !\n");
			flagOf4GEnQue = 1;
			close(remote_sockfd_4G);
			
			return ;
		}
		strcpy(MsgInfoSnd.msgText,buf);
		MsgInfoSnd.msgType = MessageTypeFor4G;
		retOfEnque = InfoEnQue(&MsgInfoSnd);
		if(retOfEnque == 1)
		{
			perror("msg EnQue for 4G error !\n");
			flagOf4GEnQue = 1;
			close(remote_sockfd_4G);
			return ;
		}
		
		close(remote_sockfd_4G);	
	}	
}

/******************************threadOfGetFromQue***************************************/
void threadOfGetInfoFromQue()
{
	char InfoData[buflen];//报文数据=报文内容+校验位,此处InfoData为去掉命令ID和结果/请求标志位后的报文内容
	char HeadOfData[4] = {'\0'};//报文内容头部的两个字节内容
	char msgReceive[buflen];//命令解析器返回的json串
	char InfoHead[33]={'\0'};//报文头部
	struct MSGSTRUCT MsgInfoRcv;
	int retOfGetFromQue = 0;
	int retOfGetDataFromSocketInfo = 0;
	int retOfSendToServer = 0;
	while(true)
	{
		
		memset(InfoHead,'\0',33);
		memset(InfoData,'\0',buflen);
		memset(HeadOfData,'\0',4);
		memset(msgReceive,'\0',buflen);
		memset(FeedbackForPPoe,'\0',buflen);
		memset(FeedbackFor4G,'\0',buflen);
		flagOfPop = 0;
		MsgId = msgget(MSGKEY,IPC_EXCL);
		if(MsgId < 0)
		{
			perror("MsgQ is not exists !\n");
			flagOfPop = 1;
			return ;
		}
		//接收消息成功返回消息长度，失败返回-1，若队列为空则阻塞
		retOfGetFromQue = msgrcv(MsgId,&MsgInfoRcv,sizeof(struct MSGSTRUCT),0,0);
		if(retOfGetFromQue == -1)
		{
			perror("msg(msgrcv) receive error !\n");
			flagOfPop = 1;
			return ;
		}
		memcpy(InfoHead,MsgInfoRcv.msgText,32);
		memcpy(HeadOfData,&MsgInfoRcv.msgText[32],3);
		retOfGetDataFromSocketInfo = getDataFromSocketInfo(MsgInfoRcv.msgText,InfoData);
		if(retOfGetDataFromSocketInfo == 1)
		{
			HeadOfData[2] = 0x01;//校验失败，报文数据不完整
		}
		else if(retOfGetDataFromSocketInfo == -1)
		{
			HeadOfData[2] = 0x03;// 验证失败，非本router命令
		}
		else
		{
			HeadOfData[2] = 0x00;//传入了命令解析器
		}
			
		/* 调用命令解析器传入InfoData, msgReceive接收返回的json*/
		/*******************************msgType = MessageTypeForPPoe**********************/
		if(MsgInfoRcv.msgType == MessageTypeForPPoe)
		{
			PrepareInfoForFeedback(InfoHead,HeadOfData,msgReceive,FeedbackForPPoe);
			retOfSendToServer = SendFeedbackToServer(FeedbackForPPoe);
			if(retOfSendToServer == 1)
			{
				perror("Send feedback of ppoe To Server error !\n");
				return ;
			}
		}
		if(MsgInfoRcv.msgType == MessageTypeFor4G)
		{
			PrepareInfoForFeedback(InfoHead,HeadOfData,msgReceive,FeedbackFor4G);
			retOfSendToServer = SendFeedbackToServer(FeedbackFor4G);
			if(retOfSendToServer == 1)
			{
				perror("Send feedback  of 4G To Server error !\n");
				return ;
			}
		}
	}
}

/****************************int InfoEnQue(MSGSTRUCT *MsgInfoSnd)**********************/
int InfoEnQue(struct MSGSTRUCT *MsgInfoSnd)
{
	int FlagOfSendToQue = 0;
	MsgId = msgget(MSGKEY,IPC_EXCL);//消息队列为空和消息队列不存在时候的返回值？
	if(MsgId < 0)
	{
		perror("Msg queue is not exists !\n");
		return 1;
	}
	FlagOfSendToQue = msgsnd(MsgId, &MsgInfoSnd, sizeof(struct MSGSTRUCT),IPC_NOWAIT);////消息入队
	if(FlagOfSendToQue < 0)
	{
		perror("send msg to queue error !\n");
		return 1;
	}
	return 0;
}

/********************************getDataFromSocketInfo(char socketInfo[],char commandInfo[])***************/
int getDataFromSocketInfo(char socketInfo[],char commandInfo[])//0获取成功，1校验失败，-1不是本路由器的指令
{
	char DeviceID[17] = {'\0'};
	char CheckFlag = 0;
	int DataLength = 0;
	int i = 0;
	char InfoHeadOfsocket[33] = {'\0'};
	char DataOfsocket[buflen] = {'\0'};
	memcpy(InfoHeadOfsocket,socketInfo,32);
	memcpy(DeviceID,&InfoHeadOfsocket[20],16);
	for(i = 0; i < 16 ; ++ i)
	    if((DeviceID[i] ^ RouterId[i]) != 0)
	    {
		perror("the commend is not for this router !\n");
		return -1;
	    }
	memcpy(&DataLength,&InfoHeadOfsocket[28],4);
	memcpy(DataOfsocket,&socketInfo[32],DataLength);
	for(i = 0; i < DataLength; ++ i)
		CheckFlag ^= DataOfsocket[i];
	if(CheckFlag != 0)
	{
		perror("The message was incomplete !\n ");
		return 1;
	}
	memcpy(commandInfo,&DataOfsocket[3],DataLength-4);
	return 0;
	
}

/********************************PrepareInfoForFeedback***************/
void PrepareInfoForFeedback(char InfoHead[], char HeadOfData[],char DataInfo[], char FeedbackInfo[])//0成功，1出错
{
	unsigned int dataLen = strlen(DataInfo);
	char DataFlag = 0;
	unsigned int i = 0;
	//计算校验位
	for(i=0; i < dataLen; ++ i)
		DataFlag ^= DataInfo[i];
	DataInfo[dataLen] = DataFlag;
	//dataLen 加四（命令ID 2byte 结果/请求标志1byte，校验位1byte）
	dataLen += 4;
	memcpy(&InfoHead[28],&dataLen,4);//将报文数据长度填到数据报头
	memcpy(FeedbackInfo,InfoHead,32);//数据报头
	memcpy(&FeedbackInfo[32],HeadOfData,3);//报文内容头部：命令ID+结果/请求标志
	memcpy(&FeedbackInfo[35],DataInfo,dataLen-3);//报文内容Data+校验位
}
/************************************void SendFeedbackToServer(unsigned char FeedbackInfo[]);*******************************/
int SendFeedbackToServer(char FeedbackInfo[])
{
	int client_sockfd;
	/*创建客户端套接字--IPv4协议，面向连接通信，TCP协议*/  
	if((client_sockfd=socket(PF_INET,SOCK_STREAM,0))<0)  
    {  
        perror("create socket of SendFeedbackToServer error ! ");  
        return 1;  
    }  
	if(connect(client_sockfd,(struct sockaddr *)&remote_addr,sizeof(struct sockaddr))<0)  
    {  
        perror("socket connect of SendFeedbackToServer error ! ");  
        return 1;  
    }  
    printf("connected to server !/n");
	send(client_sockfd,FeedbackInfo,buflen,0); 
	close(client_sockfd);
	return 0;
}
int EnrollForRouter()
{
}