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

#define IP "192.168.0.149"
#define PORT 8888

#define ERR_MSG(msg) do{\
	fprintf(stderr, "%d %s ", __LINE__, __func__);\
	perror(msg);\
}while(0)

typedef struct 
{
	int  newfd;
	struct sockaddr_in cin;
	sqlite3* db;
}TM;

typedef struct 
{
	char type;
	char name[64];
	char password[64];
	char data[512];
	char state;
}MSG;

void* do_thread(void* arg);
int do_register(int newfd, MSG msg, sqlite3* db);
int do_login(int newfd, MSG msg, sqlite3* db);
int do_cancel(int newfd, MSG msg, char* Logoutname, sqlite3* db);
int do_search(int newfd, MSG msg, char* Loginname, sqlite3* db);
int do_history(int newfd, MSG msg, char* Loginname, sqlite3* db);
int do_split_string(char* buf, char* word, char* definition);
int do_import(sqlite3* db);
int do_init_user(sqlite3* db);



int main(int argc, const char* argv[]) 
{

	//打开数据库	
	sqlite3* db = NULL;

	if(sqlite3_open("./dict1.db", &db) != 0) 
	{
		printf("sqlite3_open failed\n");
		printf("sqlite3_open:%s\n", sqlite3_errmsg(db));
		return -1;
	}
	printf("库打开成功\n");
	
	//创建三张表格
	char  sql[128] = "";
	char* errmsg;
	sprintf(sql, "create table if not exists dict1(id int primary key, word char, definition char)");
	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != 0) 
	{
		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}
	bzero(sql, sizeof(sql));
    printf("创建单词表格成功\n");
	do_import(db);


	sprintf(sql, "create table if not exists usermsg(name char primary key, password char, state char)");
	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != 0) 
	{
		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}
	bzero(sql, sizeof(sql));
    printf("创建用户表格成功\n");


	sprintf(sql, "create table if not exists records(name char, data char,time char)");
	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != 0) 
	{
		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}
	bzero(sql, sizeof(sql));
    printf("创建记录表格成功\n");

	do_init_user(db);

	//创建套接字
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd < 0) 
	{
		ERR_MSG("socket");
		return -1;
	}

	//允许重用
	int value = 1;
	if(setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) 
	{
		ERR_MSG("setsockopt");
		return -1;
	}

	//绑定
	struct sockaddr_in sin;
	sin.sin_family      = AF_INET;
	sin.sin_port        = htons(PORT);
	sin.sin_addr.s_addr = inet_addr(IP);

	if(bind(sfd, (struct sockaddr*)&sin, sizeof(sin)) < 0) 
	{

		ERR_MSG("bind");
		return -1;
	}
	printf("绑定成功\n");

	//监听
	if(listen(sfd, 20) < 0) 
	{
     	ERR_MSG("listen");
    	return -1;
	}
	printf("监听成功\n");

	//创建线程维护客户端
	struct sockaddr_in cin;
	int newfd;
	socklen_t  addrlen = sizeof(cin);
	
	while(1) 
	{
		newfd = accept(sfd, (struct sockaddr*)&cin, &addrlen);
    	if(newfd < 0) 
		{
        	ERR_MSG("accept");
	    	return -1;
    	}
    	printf("acceptd = %d\n", newfd);
    	printf("[%s:%d]connect success\n", inet_ntoa(cin.sin_addr), ntohs(cin.sin_port));
    
		TM info;
		info.newfd = newfd;
		info.cin      = cin;
		info.db       = db;


		pthread_t tid;
		if(pthread_create(&tid, NULL, do_thread, (void*)&info) != 0) 
		{
			ERR_MSG("pthread_create");
			return -1;
		} 
	}
	close(sfd);
	sqlite3_close(db);
	return 0;
}

void* do_thread(void* arg) 
{
	pthread_detach(pthread_self());
	TM                 info     = *(TM*)arg;
	int                newfd = info.newfd;
	sqlite3*           db       = info.db;
	MSG                msg;
	char               Logoutname[64];
	char               Loginname[64];
	memset(&msg, 0, sizeof(msg));
	while(recv(newfd, &msg, sizeof(msg), 0) > 0) 
	{
		switch(msg.type) 
		{
			case 'L':
				if(do_login(newfd, msg, db) == 0) 
				{
					strcat(Logoutname, msg.name);
					strcat(Loginname, msg.name);
				}
				break;
			case 'R':
				do_register(newfd, msg, db);
				break;
			case 'E':
				close(newfd);
				pthread_exit(NULL);
			case 'C':
				do_cancel(newfd, msg, Logoutname, db);
				bzero(Logoutname, sizeof(Logoutname));
				bzero(Loginname, sizeof(Loginname));
				break;
			case 'H':
				do_history(newfd, msg, Loginname, db);
				break;
			case 'Q':
				do_search(newfd, msg, Loginname, db);
				break;
			default:
				break;


		}
	}

	close(newfd);
	pthread_exit(NULL);
}

//创建
int do_register(int newfd, MSG msg, sqlite3* db) 
{

	char  sql[256] = "";
	char* errmsg   = NULL;
	printf("name = %s password = %s state = %c size = %lu\n",msg.name, msg.password, msg.state,  sizeof(msg.password));
	sprintf(sql, "insert into usermsg values('%s', '%s', '%c')", msg.name, msg.password, msg.state);

	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) != 0) 
	{
		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
		strcpy(msg.data, "USER ALREADY EXIST!");
	}
	else 
	{
		printf("REGISTER SUCCESS!\n");
		strcpy(msg.data, "OK!");
	}
	if(send(newfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	} 
	return 0;
}

//登录
int do_login(int newfd, MSG msg, sqlite3* db) 
{

	//之前注册的信息登录
	char   sql[256] = "";
 	char** result   = NULL;
	int    row, column;
	char*  errmsg   = NULL;


	sprintf(sql, "select * from usermsg where name = '%s'", msg.name);

	if(sqlite3_get_table(db, sql, &result, &row, &column, &errmsg) !=0) 
	{
		printf("sqlite3_get_table:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}
	if(row < 1) 
	{
		msg.type = 'F';
		strcpy(msg.data, "User does not exist");
		if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{
			ERR_MSG("send");
			return -1;
		}
		return -1;
	}

	sqlite3_free_table(result);
	bzero(sql,sizeof(sql));
	sprintf(sql, "select * from usermsg where name = '%s' and password = '%s'", msg.name, msg.password);

	if(sqlite3_get_table(db, sql, &result, &row, &column, &errmsg) !=0) 
	{
		printf("sqlite3_get_table:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}
    

	if(row < 1) 
	{
		msg.type = 'F';
		strcpy(msg.data, "Wrong password");
		if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{
			ERR_MSG("send");
			return -1;
		}
		return -1;
	}

	if(row == 1) {
		msg.type = 'S';
		if('F' == *result[5]) 
		{
        	strcpy(msg.data, "SIGNIN SUCCESS");
            if(send(newfd, &msg, sizeof(msg), 0) < 0) 
			{
	        	ERR_MSG("send");
	        	return -1;
			}
            sprintf(sql,"update usermsg set state = 'O' where name = '%s'", result[3]);
        	if(sqlite3_exec(db,sql,NULL,NULL,&errmsg) != 0)
			{

	        	printf("sqlite3_exec:__%d__%s\n",__LINE__,errmsg);
		        return -1;
          	}
        }
		else 
		{
			msg.type = 'F';
			strcpy(msg.data, "The account has been logged in, please log in again!");
            if(send(newfd, &msg, sizeof(msg), 0) < 0) 
			{
	        	ERR_MSG("send");
	        	return -1;
			}
			return -1;
		}
	}

    sprintf(sql,"update usermsg set state = 'O' where name = '%s'", result[3]);
	if(sqlite3_exec(db,sql,NULL,NULL,&errmsg) != 0)
	{
		printf("sqlite3_exec:__%d__%s\n",__LINE__,errmsg);
		return -1;
	}

	sqlite3_free_table(result);
	return 0;
}

//取消
int do_cancel(int newfd, MSG msg, char* Logoutname, sqlite3* db) 
{
	char   sql[256] = "";
	char*  errmsg   = NULL;

    sprintf(sql,"update usermsg set state = 'F' where name = '%s'", Logoutname);
	printf("Logoutname = %s\n", Logoutname);

	if(sqlite3_exec(db,sql,NULL,NULL,&errmsg) != 0)
	{
		printf("sqlite3_exec:__%d__%s\n",__LINE__,errmsg);
		msg.type = 'F';
    	strcpy(msg.data, "Logout Failed!");
        if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{ 
	    	ERR_MSG("send");
	    	return -1;
    	}
		return -1;
	}

	msg.type = 'S';
	strcpy(msg.data, "Logout Succeeded!");
    if(send(newfd, &msg, sizeof(msg), 0) < 0) 
	{ 
		ERR_MSG("send");
		return -1;
	}
	return 0;
}

//查找
int do_search(int newfd, MSG msg, char* Loginname, sqlite3* db) 
{
	char   sql[1024] = "";
 	char** result    = NULL;
	int    row, column;
	char*  errmsg    = NULL;
	time_t t;
	time(&t);


	sprintf(sql, "select word, definition from dict1 where word = '%s'", msg.data);

	if(sqlite3_get_table(db, sql, &result, &row, &column, &errmsg) !=0) 
	{
		printf("sqlite3_get_table:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}


	if(row < 1) 
	{
		msg.type   = 'F';
		char temp[512]="";

		strcat(temp, msg.data);
		strcat(temp, "\n");
		strcpy(msg.data, "The word does not exist!");

		if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{

			ERR_MSG("send");
			return -1;
		}
    	bzero(sql, sizeof(sql));
    	sprintf(sql, "insert into records values('%s', '%s','%s')", Loginname, temp, ctime(&t));

    	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) !=0) 
		{

	    	printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
	    	return -1;
    	}

    	printf("Insert Data Success\n");
		return -1;

	}
	memset(&msg, 0, sizeof(msg));

	int line=0 , list=0;
	for(line=1; line<(row+1); line++) 
	{
		for(list=0; list<column; list++) 
		{
			strcat(strcat(msg.data, result[line*column+list]),"\t");
		}
		printf("%s",strcat(msg.data, "\n"));
	}
	msg.type = 'S';
	if(send(newfd, &msg, sizeof(msg), 0) < 0) 
	{

		ERR_MSG("send");
		return -1;
	}

	sqlite3_free_table(result);

	bzero(sql, sizeof(sql));
	sprintf(sql, "insert into records values('%s', '%s','%s')", Loginname, msg.data, ctime(&t));

	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) !=0) 
	{
		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}

	printf("Insert Data Success\n");
	return 0;
}


//历史记录
int do_history(int newfd, MSG msg, char* Loginname, sqlite3* db) 
{
	char   sql[1024] = "";
 	char** result    = NULL;
	int    row, column;
	char*  errmsg    = NULL;

	sprintf(sql, "select data, time from records where name = '%s'", Loginname);

	if(sqlite3_get_table(db, sql, &result, &row, &column, &errmsg) !=0) 
	{
		printf("sqlite3_get_table:__%d__ %s\n", __LINE__, errmsg);
		return -1;
	}


	memset(&msg, 0, sizeof(msg));
	if(row < 1) 
	{
		msg.type = 'F';
		strcpy(msg.data, "History does not exist!");
		if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{
			ERR_MSG("send");
			return -1;
		}
		return -1;

	}
	int line = 0 , list = 0;
    msg.type = 'S';
	for(line=1; line<(row+1); line++) 
	{
		bzero(msg.data, sizeof(msg.data));
		for(list=0; list<column; list++) 
		{
			strcat(msg.data, result[line*column+list]);
		}
		strcat(msg.data, "\n");
    	msg.type = 'S';
    	if(send(newfd, &msg, sizeof(msg), 0) < 0) 
		{
	    	ERR_MSG("send");
     		return -1;
    	}
	}
    msg.type = '0';
	if(send(newfd, &msg, sizeof(msg), 0) < 0) 
	{
		ERR_MSG("send");
		return -1;
	}
	sqlite3_free_table(result);
	return 0;
}

int do_import(sqlite3* db) 
{
    int id = 1;
	char word[32];
	char definition[128];
	char buf[164];

	FILE* fp = fopen("./dict.txt", "r");
	if(NULL == fp) 
	{
		ERR_MSG("fopen");
		return -1;
	}

	while(fgets(buf, 164, fp) != NULL) 
	{
		bzero(word, sizeof(word));
		bzero(definition, sizeof(definition));
		do_split_string(buf, word, definition);
		definition[strlen(definition) - 1] = 0;		

    	char* errmsg   = NULL;
    	char* sql;
		if((sql = sqlite3_mprintf("insert into dict1 values(%d, '%q', '%q')", id, word, definition)) == NULL) 
		{
			printf("sqlite3_mprintf:__%d__ %s\n", __LINE__, errmsg);
			return -1;
		}
    	if(sqlite3_exec(db, sql, NULL, NULL, &errmsg) !=0) 
		{
     		printf("sqlite3_exec:__%d__ %s\n", __LINE__, errmsg);
    		return -1;
    	}
		sqlite3_free(sql);
		id++;
		bzero(word, sizeof(word));
		bzero(definition, sizeof(definition));
    }

	printf("数据导入成功\n");
	fclose(fp);
	return 0;
}

//字符串分裂
int do_split_string(char* buf, char* word, char* definition) 
{
	const char s[2]    = " ";
	char*      token;
	buf[strlen(buf)-1] = 0;

	token = strtok(buf, s);
	strcat(word, buf);

	while(token != NULL) 
	{
		token = strtok(NULL, s);
		if(token == NULL) 
		{
			break;
		}
		strcat(strcat(definition, token), s);
	}
	return 0;
}


//初始化
int do_init_user(sqlite3* db) 
{
	char *errmsg = NULL;
	char sql[256] = "";

    sprintf(sql,"update usermsg set state = 'F'");

	if(sqlite3_exec(db,sql,NULL,NULL,&errmsg) != 0) 
	{
		printf("sqlite3_exec:__%d__%s\n",__LINE__,errmsg);
		return -1;
	}
	return 0;
}

