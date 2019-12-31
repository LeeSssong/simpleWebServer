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


//接收客户端的连接，并读取请求数据
void *accept_request(void* from_client)
{
	int client = *(int *)from_client;
	char buf[1024];
	int numchars;
	char method[255];
	char url[255];
	char path[512];
	size_t i, j;
	struct stat st;

	//标志位，若为post或get请求，则为1
	int cgi = 0;
	
	char *query_string = NULL;

	//处理第一行http信息
	numchars = get_line(client, buf, sizeof(buf));
	i = 0; j = 0;

	 //对于HTTP报文来说，第一行的内容即为报文的起始行，格式为<method> <reque	  st-URL> <version>
	 //每个字段用空白字符相连
	 //如果请求网址为http://172.0.0.1:端口号/idnex.html
	 //那么得到的第一条http为
	 //GET /index.html HTTP/1.1
	 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 	{
	//提取其中的请求方式是GET还是POST
	 method[i] = buf[j];
  	 i++; j++;
 	}
	 method[i] = '\0';

	 //函数说明：strcasecmp()用来比较参数s1 和s2 字符串，比较时会自动忽略大			    小写的差异。
	 //返回值：若参数s1 和s2 字符串相同则返回0。s1 长度大于s2 长度则返回大于	 	   0 的值，s1 长度若小于s2 长度则返回小于0 的值。
	 if( strcasecmp(method, "GET") && strcasecmp(method, "POST"))
	 {
		 unimplement(client);
		 return NULL;
	 }

	 //cgi为标志位，此时开启cgi解析
	 if (strcasecmp(method, "POST") == 0)
		 cgi = 1;
	
	 i = 0;

	 //将method后面的空白字符串过滤
	 while (ISspace(buf[j]) && (j < sizeof(buf)))
		 j++;

	 //读取url
	 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 	 {
  	  url[i] = buf[j];
  	  i++; j++;
 	 }
	 url[i] = '\0';
	
	 //若请求方式为get,则可能会带有?参数查询
	 if (strcasecmp(method, "GET") == 0)
	 {
		 query_string = url;
		 while ((*query_string != '?') && (*query_string != '\0'))
		 {
			 query_string++;

		 }
		 if (*query_string == '?')
		 {
			 //使用cgi处理参数
			 cgi = 1;
			 
			 //截取参数
			 *query_string = '\0';
			 query_string ++;

		 }
	 }

	 //将url中的路径格式转化到path
	 sprintf(path, "htdocs%s", url);

	 //默认地址，解析到的路径如果为/，则自动加上index.html
	 if (path[strlen(path) -1] == '/')
		 strcat(path,"index.html");

	 //int stat(const char *file_name, struct stat *buf)
	 //通过文件名filename获取文件信息，并保存在buf所指的结构体stat中
	 //返回值： 成功则0，失败则-1，错误代码存于errno（需要include <errno.h>		   ）
	 if (stat(path,&st) == -1)
	 {
		 //如果访问的网页不存在，则不断读取剩下的请求头信息，并丢弃
		 while ((numchars >0) && ("\n",buf))
			 numchars = get_line(client, buf, sizeof(buf));

		 //声明网页不存在
		 not_found(client);
	 }
	 else
	 {
		 //如果网页存在则进行处理
		 if ((st.st_mode & S_IFMT) == S_IFDIR) //S_IFDIR代表目录
			 //如果路径是目录，则显示主页
			 strcat(path, "index.html");
		 if ((st.st_mode & S_IXUSR) ||
      		     (st.st_mode & S_IXGRP) ||
      	             (st.st_mode & S_IXOTH))
      	             //S_IXUSR:文件所有者具可执行权限
                     //S_IXGRP:用户组具可执行权限
                     //S_IXOTH:其他用户具可读取权限
		     cgi = 1;
		 if( !cgi )
			 //如果请求的不需要cgi处理，则返回静态网页
			 serve_file(client, path);
		 else
			 //执行cgi动态解析
			 execute_cgi(client, path, method, query_string);
	 }
	 //关闭
	 close(client);
	 return NULL;
}

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
	
