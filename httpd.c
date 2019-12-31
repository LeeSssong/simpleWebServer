#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ctype.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/wait.h>
#include <stdlib.h>

//检查参数c是否为空字符串
//返回值：若为空则返回非0，否则返回0
#define ISspace(x) isspace((int)(x))

//定义server名称
#define SERVER_STRING "Server: jdbhttpd/0.1.0\r\n"

//开启tcp连接，绑定端口等操作
int startup(u_short *);

//每次接受到请求，都创建一个子线程来处理，实现多并发
//把client_sock转换成地质作为参数传入pthread_create()
void *accept_request(void* client);

//执行cgi脚本
void execute_cgi(int, const char *, const char *, const char *);


//启动服务端
int startup(u_short *port)
{
	int httpd = 0;
	struct sockaddr_in name;

	//设置http socket
	//返回socket描述符
	//SOCK_STREAM:TCP协议，0:自断选择协议
	httpd = socket(PF_INET, SOCK_STREAM, 0);
	
	//如果没有获得套接字则返回-1
	if (httpd == -1)
	{
		error_die("socket error");
	}

	//初始化name各个字节为0，防止有未初始化的垃圾值存在
	memset(&name, 0, sizeof(name));

	name.sin_family = AF_INET;
	name.sin_port = htons(*port);

	//INADDR_ANY表示任何网络地址都可以访问
	name.sin_addr.s_addr = htonl(INADDR_ANY);

	//绑定端口
	if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
	  error_die("bind");

	//如果端口没有设置，提供个随机端口
	 if (*port == 0)  /*动态分配一个端口 */
	{
	 socklen_t  namelen = sizeof(name);
	 if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
	   error_die("getsockname");
	 *port = ntohs(name.sin_port);
	}

}


//main
int main()
{
	int server_sock = -1;
	u_short port = 0;
	int client_sock = -1;

	//struct sockaddr_in{
	//__uint8_t sin_len;
	//sa_family_t sin_family;
	//in_port_t sin_port;
	//struct in_addr sin_addr;
	//char	sin_zero[8];


	struct sockaddr_in client_name;

	//socklen_t类型
	socklen_t client_name_len = sizeof(client_name);
	pthread_t newthread;
	
	//启动server socket，启动监听，返回监听端口
	server_sock = startup(&port);

	printf("httpd running on port %d\n", port);

	while(1)
	{
		//接受请求，函数原型
		//#include <sys/types.h>
		//#include <sys/socket.h>
		//int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);	 
		client_sock =  accept(server_sock, (struct sockaddr *) &client_name,&client_name_len);
		
		if(client_sock == -1)
		  error_die("accept wrong");

		//将客户端套接字转成地址作为参数，启动线程处理新的连接
		if (pthread_create(&newthread , NULL, accept_request, (void*)&client_sock) != 0)
   perror("pthread_create");
	}
	
	//关闭server_sock;
	close(server_sock);

	return(0);
}
	
