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
void accept_request(int client);

//执行cgi脚本
void execute_cgi(int, const char *, const char *, const char *);

//如果不是cgi文件，直接读取文件返回给请求http的客户端
void serve_file(int, const char *);


//错误请求
void bad_request(int);

//读取文件
void cat(int, FILE *);

//无法执行
void cannot_execute(int);

//错误输出
void error_die(const char *);

//得到一行数据,只要发现c为\n,就认为是一行结束，如果读到\r,再用MSG_PEEK的方式读入一个字符，如果是\n，从socket用读出
//如果是下个字符则不处理，将c置为\n，结束。如果读到的数据为0中断，或者小于0，也视为结束，c置为\n
int get_line(int, char *, int);

//返回http头
void headers(int, const char *);

//没有发现文件
void not_found(int);

//如果不是Get或者Post，就报方法没有实现
void unimplemented(int);


//接收客户端的连接，并读取请求数据
void accept_request(int client)
{
 char buf[1024];
 int numchars;
 char method[255];
 char url[255];
 char path[512];
 size_t i, j;
 struct stat st;
 int cgi = 0;      /* becomes true if server decides this is a CGI
                    * program */
 char *query_string = NULL;

 //读http 请求的第一行数据（request line），把请求方法存进 method 中
 numchars = get_line(client, buf, sizeof(buf));
 i = 0; j = 0;
 while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
 {
  method[i] = buf[j];
  i++; j++;
 }
 method[i] = '\0';

 //如果请求的方法不是 GET 或 POST 任意一个的话就直接发送 response 告诉客户端没实现该方法
 if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
 {
  unimplemented(client);
  return;
 }

 //如果是 POST 方法就将 cgi 标志变量置一(true)
 if (strcasecmp(method, "POST") == 0)
  cgi = 1;

 i = 0;
 //跳过所有的空白字符(空格)
 while (ISspace(buf[j]) && (j < sizeof(buf))) 
  j++;
 
 //然后把 URL 读出来放到 url 数组中
 while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf)))
 {
  url[i] = buf[j];
  i++; j++;
 }
 url[i] = '\0';

 //如果这个请求是一个 GET 方法的话
 if (strcasecmp(method, "GET") == 0)
 {
  //用一个指针指向 url
  query_string = url;
  
  //去遍历这个 url，跳过字符 ？前面的所有字符，如果遍历完毕也没找到字符 ？则退出循环
  while ((*query_string != '?') && (*query_string != '\0'))
   query_string++;
  
  //退出循环后检查当前的字符是 ？还是字符串(url)的结尾
  if (*query_string == '?')
  {
   //如果是 ？ 的话，证明这个请求需要调用 cgi，将 cgi 标志变量置一(true)
   cgi = 1;
   //从字符 ？ 处把字符串 url 给分隔会两份
   *query_string = '\0';
   //使指针指向字符 ？后面的那个字符
   query_string++;
  }
 }

 //将前面分隔两份的前面那份字符串，拼接在字符串htdocs的后面之后就输出存储到数组 path 中。相当于现在 path 中存储着一个字符串
 sprintf(path, "htdocs%s", url);
 
 //如果 path 数组中的这个字符串的最后一个字符是以字符 / 结尾的话，就拼接上一个"index.html"的字符串。首页的意思
 if (path[strlen(path) - 1] == '/')
  strcat(path, "index.html");
 
 //在系统上去查询该文件是否存在
 if (stat(path, &st) == -1) {
  //如果不存在，那把这次 http 的请求后续的内容(head 和 body)全部读完并忽略
  while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
   numchars = get_line(client, buf, sizeof(buf));
  //然后返回一个找不到文件的 response 给客户端
  not_found(client);
 }
 else
 {
  //文件存在，那去跟常量S_IFMT相与，相与之后的值可以用来判断该文件是什么类型的
  //S_IFMT参读《TLPI》P281，与下面的三个常量一样是包含在<sys/stat.h>
  if ((st.st_mode & S_IFMT) == S_IFDIR)  
   //如果这个文件是个目录，那就需要再在 path 后面拼接一个"/index.html"的字符串
   strcat(path, "/index.html");
   
   //S_IXUSR, S_IXGRP, S_IXOTH三者可以参读《TLPI》P295
  if ((st.st_mode & S_IXUSR) ||       
      (st.st_mode & S_IXGRP) ||
      (st.st_mode & S_IXOTH)    )
   //如果这个文件是一个可执行文件，不论是属于用户/组/其他这三者类型的，就将 cgi 标志变量置一
   cgi = 1;
   
  if (!cgi)
   //如果不需要 cgi 机制的话，
   serve_file(client, path);
  else
   //如果需要则调用
   execute_cgi(client, path, method, query_string);
 }

 close(client);
}

void bad_request(int client)
{
	char buf[1024];
	sprintf(buf, "HTTP/1.0 400 BAD REQUEST\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "<P>Your browser sent a bad request, ");
	send(client, buf, sizeof(buf), 0);
	sprintf(buf, "such as a POST without a Content-Length.\r\n");
	send(client, buf, sizeof(buf), 0);
}

//得到内容，发送
void cat(int client, FILE *resource)
{
	char buf[1024];

	//从文件文件描述符中读取指定内容
	fgets(buf, sizeof(buf), resource);
	while (!feof(resource))
	{
		send(client, buf, strlen(buf), 0);
		fgets(buf, sizeof(buf), resource);
	}
}

void cannot_execute(int client)
{
 char buf[1024];

	sprintf(buf, "HTTP/1.0 500 Internal Server Error\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<P>Error prohibited CGI execution.\r\n");
	send(client, buf, strlen(buf), 0);
}

void error_die(const char *sc)
{
 //包含于<stdio.h>,基于当前的 errno 值，在标准错误上产生一条错误消息。参考《TLPI》P49
 perror(sc); 
 exit(1);
}



//调用cgi
void execute_cgi(int client, const char *path,const char *method, const char *query_string)
{
	//缓冲区大小
	char buf[1024];

	//2根管道
	int cgi_output[2];
	int cgi_input[2];

	//进程pid与状态
	pid_t pid;
	int status;

	
	int i;
	char c;

	//读取字符数
	int numchars = 1;

	//http的content_length；
	int content_length = -1;

	//默认字符
	buf[0] = 'A'; buf[1] = '\0';

	//如果是 http 请求是 GET 方法的话读取并忽略请求剩下的内容
	if (strcasecmp(method, "GET") == 0)
	//读取数据，把整个header都读掉，以为Get写死了直接读取index.html，没有必要分析余下的http信息了
	  while ((numchars > 0) && strcmp("\n", buf))
		numchars = get_line(client, buf, sizeof(buf));
	else
	{
		//只有 POST 方法才继续读内容
		numchars = get_line(client, buf, sizeof(buf));
		//				这个循环的目的是读出指示body长度大小的参数，并记录body的长度大小。其余的 header 里面的参数一律忽略
		//注意这里只读完 header 的内容，body 的内容没有读
		while ((numchars > 0) && strcmp("\n", buf))
		{
			//如果是POST请求，就需要得到Content-Length，Content-Length：这个字符串一共长为15位，所以
			//取出头部一句后，将第16位设置结束符，进行比较
			//第16位置为结束
			 buf[15] = '\0';
			 if (strcasecmp(buf, "Content-Length:") == 0)
			//内存从第17位开始就是长度，将17位开始的所有字符串转成整数就是content_length
			   content_length = atoi(&(buf[16])); //记录 body 的长度大小
			 numchars = get_line(client, buf, sizeof(buf));
		}
		//如果 http 请求的 header 没有指示 body 长度大小的参数，则报错返回
		if (content_length == -1) {
			bad_request(client);
			return;
		}
	}

	sprintf(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	//建立output管道
	if (pipe(cgi_output) < 0) {
		cannot_execute(client);
		return;
	}
	//建立input管道
	if (pipe(cgi_input) < 0) {
		cannot_execute(client);
		return;
	}

	//       fork后管道都复制了一份，都是一样的
	//       子进程关闭2个无用的端口，避免浪费
	//       ×<------------------------->1    output
	//       0<-------------------------->×   input
	//
	//       父进程关闭2个无用的端口，避免浪费
	//       0<-------------------------->×   output
	//       ×<------------------------->1    input
	//       此时父子进程已经可以通信
	
	//fork进程，子进程用于执行CGI
	//父进程用于收数据以及发送子进程处理的回复数据
	if ( (pid = fork()) < 0 ) {
		cannot_execute(client);
		return;
	}

	if (pid == 0)
	{
		char meth_env[255];
		char query_env[255];
		char length_env[255];

		//将子进程的输出由标准输出重定向到 cgi_ouput 的管道写端上
		dup2(cgi_output[1], 1);
		//将子进程的输出由标准输入重定向到 cgi_ouput 的管道读端上
		dup2(cgi_input[0], 0);
		//关闭 cgi_ouput 管道的读端与cgi_input 管道的写端
		close(cgi_output[0]);
		close(cgi_input[1]);

		//构造一个环境变量
		sprintf(meth_env, "REQUEST_METHOD=%s", method);
		//将这个环境变量加进子进程的运行环境中
		putenv(meth_env);

		//根据http 请求的不同方法，构造并存储不同的环境变量
		if (strcasecmp(method, "GET") == 0) {
			sprintf(query_env, "QUERY_STRING=%s", query_string);
			putenv(query_env);
		}
		 else {   /* POST */
			 sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
			 putenv(length_env);
		 }

		 //最后将子进程替换成另一个进程并执行 cgi 脚本
		 execl(path, path, NULL);
		 exit(0);
		 } 
	else {    /* parent */
		 //父进程则关闭了 cgi_output管道的写端和 cgi_input 管道的读端
		 close(cgi_output[1]);
		 close(cgi_input[0]);

		 //如果是 POST 方法的话就继续读 body 的内容，并写到 cgi_input 管道里让子进程去读
		 if (strcasecmp(method, "POST") == 0)
		   for (i = 0; i < content_length; i++) {
			   recv(client, &c, 1, 0);
			   write(cgi_input[1], &c, 1);
		   }

		 //然后从 cgi_output 管道中读子进程的输出，并发送到客户端去
		 while (read(cgi_output[0], &c, 1) > 0)
		   send(client, &c, 1, 0);
		 //关闭管道
		 close(cgi_output[0]);
		 close(cgi_input[1]);

		 //等待子进程的退出
		 waitpid(pid, &status, 0);
	}
}

int get_line(int sock, char *buf, int size)
{
	int i = 0;
	char c = '\0';
	int n;
	while ((i < size - 1) && (c != '\n'))
	{
		//recv()包含于<sys/socket.h>,参读《TLPI》P1259,
		//读一个字节的数据存放在 c 中
		n = recv(sock, &c, 1, 0);
		if (n > 0)
		{
			if (c == '\r')
			{
				//如果是\n就读走
				n = recv(sock, &c, 1, MSG_PEEK);
				if ((n > 0) && (c == '\n'))
					recv(sock, &c, 1, 0);
				else
				//不是\n（读到下一行的字符）或者没读到，置c为\n 跳出循环,完成一行读取
					c = '\n';
			}
			buf[i] = c;
			i++;
		}
		else
		  c = '\n';
	}
	buf[i] = '\0';
	return(i);
}
//加入http的headers
void headers(int client, const char *filename)
{
	char buf[1024];
	(void)filename;  /* could use filename to determine file type */

	strcpy(buf, "HTTP/1.0 200 OK\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	strcpy(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
}
//如果资源没有找到得返回给客户端下面的信息
void not_found(int client)
{
	char buf[1024];

	sprintf(buf, "HTTP/1.0 404 NOT FOUND\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><TITLE>Not Found</TITLE>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>The server could not fulfill\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "your request because the resource specified\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "is unavailable or nonexistent.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
}


//如果不是CGI文件，直接读取文件返回给请求的http客户端
void serve_file(int client, const char *filename)
{
	FILE *resource = NULL;
	int numchars = 1;
	char buf[1024];

	//确保 buf 里面有东西，能进入下面的 while 循环
	buf[0] = 'A'; buf[1] = '\0';
	//循环作用是读取并忽略掉这个 http 请求后面的所有内容
	while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
	  numchars = get_line(client, buf, sizeof(buf));
	//打开这个传进来的这个路径所指的文件
	resource = fopen(filename, "r");
	if (resource == NULL)
	  not_found(client);
	else
	{
	//打开成功后，将这个文件的基本信息封装成 response 的头部(header)并返回
		headers(client, filename);
	//接着把这个文件的内容读出来作为 response 的 body 发送到客户端
		cat(client, resource);
	}
	fclose(resource);
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
	 int namelen = sizeof(name);
	 if (getsockname(httpd, (struct sockaddr *)&name, &namelen) == -1)
	   error_die("getsockname");
	 *port = ntohs(name.sin_port);
	}
	//开始监听
	if (listen(httpd, 5) < 0)
	  error_die("listen");
	//返回socket id
	return(httpd);

}

//如果请求方法没有实现，就返回此信息
void unimplemented(int client)
{
	char buf[1024];
	sprintf(buf, "HTTP/1.0 501 Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, SERVER_STRING);
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "Content-Type: text/html\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</TITLE></HEAD>\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "<BODY><P>HTTP request method not supported.\r\n");
	send(client, buf, strlen(buf), 0);
	sprintf(buf, "</BODY></HTML>\r\n");
	send(client, buf, strlen(buf), 0);
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
	int client_name_len = sizeof(client_name);
	//pthread_t newthread;
	
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
		  error_die("accept");

		accept_request(client_sock);

		//将客户端套接字转成地址作为参数，启动线程处理新的连接
		/*if (pthread_create(&newthread , NULL, accept_request, (void*)&client_sock) != 0)
   perror("pthread_create");*/
	}
	
	//关闭server_sock;
	close(server_sock);

	return(0);
}
	
