#include <stdio.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#define ERR_MSG(msg) do{\
    fprintf(stderr, "%d %s %s\n", __LINE__, __func__, __FILE__);\
    perror(msg);\
}while(0)


typedef struct 
{

	char type;           //R: register | L: sign in | H: history | Q: search | S; success | F: fail | C; cancel | E ; exit
	char name[64];
	char password[64];
	char data[512];
	char state;         // O : online   F : offline
}MSG;

char pname[64];
int  do_register(int sfd, MSG msg);
int  do_login(int sfd, MSG msg);
int  do_quit(int sfd, MSG msg);
int  do_cancel(int sfd, MSG msg);
int  do_search(int sfd, MSG msg);
int  do_history(int sfd, MSG msg);



int main(int argc, const char *argv[]) 
{

	if(argc < 3) 
	{
		printf("输入ＩＰ和端口号\n");
		return -1;
	}
	
	//创建套接字
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd < 0) 
	{
		ERR_MSG("socket");
		return -1;
	}
	
    //连接

	struct sockaddr_in sin;

	sin.sin_family      = AF_INET;
	sin.sin_port        = htons(atoi(argv[2]));
	sin.sin_addr.s_addr = inet_addr(argv[1]);

	if(connect(sfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) 
	{
		ERR_MSG("connect");
		return -1;
	}
	printf("connect server success\n");

	//选项

	MSG msg;
	int choose = 0;

head:
	while(1) 
	{
		system("clear");
		printf("**********************\n");
		printf("******************** *\n");
		printf("******* 1.注册 *******\n"); 
		printf("**********************\n");
		printf("**********************\n");
		printf("******  2.登录 *******\n");
		printf("**********************\n");
		printf("**********************\n");
		printf("******* 3.退出 *******\n");
		printf("**********************\n");

		putchar(10);

		printf("请输入选项 >>>>>");
		scanf("%d", &choose);
		while(getchar() != 10);
		memset(&msg, 0, sizeof(msg));

		switch(choose) 
		{
	    	case 1:
				do_register(sfd,msg);
				break;
    		case 2:
    			if(do_login(sfd,msg)==1)
				{
					goto next;

				}	

				break;
	    	case 3:
		    	do_quit(sfd, msg);
				return 0;
	    	default:
     			printf("Input Again\n");
		    	continue;
		}
	}



next:

	while(1) 
	{
		
		system("clear");
		printf("**********************\n");
		printf("******************** *\n");
		printf("******* 1.查询 *******\n"); 
		printf("**********************\n");
		printf("**********************\n");
		printf("******  2.历史 *******\n");
		printf("**********************\n");
		printf("**********************\n");
		printf("******  3.退出 *******\n");
		printf("**********************\n");
		putchar(10);

		printf("请输入选项 >>>>>");	
		scanf("%d", &choose);
		while(getchar() != 10);

		switch(choose) 
		{

	    	case 1:
		    	do_search(sfd,msg);
	    		break;
    		case 2:
  			    do_history(sfd,msg);
	    		break;
	    	case 3:
	            if(do_cancel(sfd,msg) == 1) 
				{
					goto head;
				}
		    	break;
	    	default:
     			printf("Input Again\n");
		    	continue;
		}
        printf("输入任意数字清屏>>>!");
		while(getchar() != 10);
	}
    return 0;
}

//注册
int do_register(int sfd, MSG msg) 
{

    //填写用户名密码
	msg.type = 'R';

	printf("请输入你的用户名 >>>");
	fgets(msg.name, sizeof(msg.name), stdin);
	msg.name[strlen(msg.name)-1] = 0;

	printf("请输入你的密码 >>>");
	fgets(msg.password, sizeof(msg.password), stdin);
	msg.password[strlen(msg.password)-1] = 0;

	msg.state = 'F';

	//发送服务器
	if(send(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}

	//从服务器接收
	if(recv(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("recv");
		return -1;
	}
	printf("%s\n", msg.data);

	printf("输入任意数字清屏>>>!");
	while(getchar() != 10);

	return 0;
}

//登录
int do_login(int sfd, MSG msg) 
{

    //填写账号密码
	msg.type = 'L';
	

	printf("请输入你的用户名 >>>");
	fgets(msg.name, sizeof(msg.name), stdin);
	msg.name[strlen(msg.name)-1] = 0;

	printf("请输入你的密码 >>>");
	fgets(msg.password, sizeof(msg.password), stdin);
	msg.password[strlen(msg.password)-1] = 0;


	//发送给服务器
	if(send(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}

	//从服务器接收
	if(recv(sfd, &msg, sizeof(msg), 0) < 0)
	{
		ERR_MSG("recv");
		return -1;
	}
	switch(msg.type) 
	{
		case 'S':
			printf("%s\n", msg.data);
			strcat(pname, msg.name);
			return 1;
			break;
		case 'F':
			printf("%s\n", msg.data);
			printf("输入任意数字返回!");
			while(getchar() != 10);
			return -1;
			break;
		default:
			break;
	}
	return 0;
}


//退出
int do_quit(int sfd, MSG msg) 
{
	msg.type = 'E';

	if(send(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}
	return 0;
}

//取消
int do_cancel(int sfd, MSG msg) 
{

	msg.type  = 'C';

	//发送给服务器
	if(send(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}

	//从服务器接收
	if(recv(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("recv");
		return -1;
	}

	switch(msg.type) 
	{
		case 'S':
			printf("%s\n", msg.data);
			return 1;
			break;
		case 'F':
			printf("%s\n", msg.data);
			printf("输入任意数字清屏!");
			while(getchar() != 10);
			return -1;
			break;
		default:
			break;
	}

	return 0;
}

//查找单词
int do_search(int sfd, MSG msg) 
{

	while(1) 
	{
		system("clear");
    	printf("___________\n");
    	printf("___________\n");
    	printf("请输入你要查找的单词　按#返回！！>>>");
    	fgets(msg.data, sizeof(msg.data), stdin);
    	msg.data[strlen(msg.data)-1] = 0;
		msg.type = 'Q';
	
    	if(strncasecmp(msg.data, "#", 1) == 0) 
		{
	    	break;
    	}
		if(send(sfd, &msg, sizeof(msg), 0) < 0) 
		{
			ERR_MSG("send");
			return -1;
		}
		if(recv(sfd, &msg, sizeof(msg), 0) < 0) 
		{
			ERR_MSG("recv");
			return -1;
		}

    	switch(msg.type) 
		{
        	case 'S':
            	printf("%s", msg.data);
            	printf("输入任意数字返回上一级!");
            	while(getchar() != 10);
	         	break;
        	case 'F':
            	printf("%s\n", msg.data);
            	printf("输入任意数字返回上一级!");
            	while(getchar() != 10);
	         	break;
        	default:
	        	break;
    	}		
	}
	return 0;
}

//历史记录
int do_history(int sfd, MSG msg) 
{

	msg.type = 'H';
	if(send(sfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}
    while(1) 
	{

    	if(recv(sfd, &msg, sizeof(msg), 0) < 0) 
		{
	    	ERR_MSG("recv");
	    	return -1;
    	}

        switch(msg.type) 
		{
            case 'S':
				printf("%s\n", msg.data);
	            continue;
            case 'F':
                printf("%s\n", msg.data);
	            return 0;
            default:
	            break;
        }
		break;
	}

	return 0;
}

