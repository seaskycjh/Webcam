#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "mjpg.h"
#include "output.h"
#include "httpd.h"

server_t *server;
extern global_t global;

void *server_thread( void *arg);
void *client_thread(void *arg);

static void read_arg(char *buf){
	char *arg = NULL;  //参数
	size_t len = strlen(buf);
	int val = 0;
	if((arg = strstr(buf, "port"))){
		arg += 5;
        if(sscanf(arg, "%d", &val)) server->port = val;
	}else if((arg = strstr(buf, "www_folder"))){
		arg += 11;
		server->www_folder = (char *)malloc(len+1);
        strcpy(server->www_folder, arg);
	}
}

int output_init(void){
    int conf_fd = 0;
    char buf[64] = {0};
    int i = 0;
    char c = 0;

    server = (server_t *)malloc(sizeof(server_t));
    if(server == NULL){
        perror("malloc server");
        exit(-1);
    }
    server->fd = 0;
    server->www_folder = NULL;
    server->port = 0;

    conf_fd = open("mjpg.conf", O_RDONLY, 0);
    if(conf_fd < 0){
        perror("open conf");
        exit(1);
    }

	//读取服务器配置信息
	while(read(conf_fd, &c, 1)){
		if(c == '\r' || c == '\n'){
			buf[i] = '\0';
			//printf("%s\n", buf);
			read_arg(buf);
			i = 0;  //restart read line
			continue;
		}
		buf[i++] = c;
	}
    printf("Port: %d\n", server->port);
    
    return 0;
    
}

int output_run(void){
    pthread_create(&server->threadID, NULL, server_thread, NULL);
    pthread_detach(server->threadID);
    return 0;
}

int output_stop(void){
    pthread_cancel(server->threadID);
    return 0;
}

void server_cleanup(void *arg) {
    close(server->fd);
    if(server->www_folder){
        free(server->www_folder);
        server->www_folder = NULL;
    }
}

void *server_thread( void *arg){
  struct sockaddr_in addr, client_addr;
  int on = 1;
  pthread_t client;
  int client_fd = 0;
  socklen_t addr_len = sizeof(struct sockaddr_in);

  /* 设置清理资源回调函数 */
  pthread_cleanup_push(server_cleanup, NULL);

  /* 创建服务器socket文件 */
  server->fd = socket(AF_INET, SOCK_STREAM, 0);
  if ( server->fd < 0 ) {
    printf("socket failed\n");
    exit(-1);
  }

  /* 地址重用 */
  if (setsockopt(server->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
    perror("setsockopt failed");
    exit(EXIT_FAILURE);
  }

  /* 保持连接属性 */
  /* setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)); */

  /* 设置服务器地址 */
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server->port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(server->fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    perror("bind failed");
    exit(-1);
  }

  /* 监听客户端连接 */
  if ( listen(server->fd, 10) != 0 ) {
    perror("listen failed");
    exit(-1);
  }

  /* 为每个客户端连接创建一个线程 */
  while(!global.stop ){
    printf("waiting for clients to connect\n");
    client_fd = accept(server->fd, (struct sockaddr *)&client_addr, &addr_len);

    /* start new thread that will handle this TCP connected client */
    printf("create thread to handle connection\n");
    printf("serving client: %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    if(client_fd != -1){
        int *tmp = (int *)malloc(sizeof(int));
        *tmp = client_fd;
        pthread_create(&client, NULL, client_thread, tmp);
        pthread_detach(client);
    }
  }

  printf("leaving server thread and cleanup\n");
  pthread_cleanup_pop(1);

  return NULL;
}


void *client_thread(void *arg){
    int fd = 0;
    int cnt = 0, i;
    char buffer[BUFFER_SIZE]={0}, *pb = NULL;
    request req = {0};

    if (arg != NULL){
        fd = *((int *)arg);
        free((int *)arg);
    }else return NULL;
    printf("client fd: %d\n", fd);

    /* 读取请求行 */
    memset(buffer, 0, sizeof(buffer));
    if((cnt = _readline(fd, buffer, sizeof(buffer))) == -1) {
        close(fd);
        return NULL;
    }
    printf("%s", buffer);
    
    if((pb = strchr(buffer, ' '))){
        pb++;
        for(i = 0; pb[i] != ' ' && pb[i]; i++){
        req.strurl[i] = pb[i];
        }
    }
    printf("%s\n", req.strurl);

    //提取请求中的参数
    if((req.strarg = strchr(req.strurl, '?'))){
        req.strarg++;
        printf("%s\n", req.strarg);
    }
  
    //判断请求类型
    if(strstr(req.strurl, "action=snapshot") != NULL) {
        req.type = A_SNAPSHOT;
    }
    else if(strstr(req.strurl, "action=stream") != NULL) {
        req.type = A_STREAM;
    }
    else if(strstr(req.strurl, "action=command") != NULL) {
        req.type = A_COMMAND;
    }
    else{
        req.type = A_FILE;
    }

    /* 解析其余的HTTP请求，获取用户名和密码
    * 请求头的结尾是单行的"\r\n"
    * 注意: 如果没有读取fd的全部内容，会产生错误 */
    do{
        memset(buffer, 0, sizeof(buffer));

        if ( (cnt = _readline(fd, buffer, sizeof(buffer))) == -1 ) {
        close(fd);
        return NULL;
        }
        //printf("%s", buffer);
    }while(cnt > 2 && !(buffer[0] == '\r' && buffer[1] == '\n'));

    /* 响应请求 */
    switch(req.type){
        case A_SNAPSHOT:
        printf("Request for snapshot\n");
        send_snapshot(fd);
        break;
        case A_STREAM:
        printf("Request for stream\n");
        send_stream(fd);
        break;
        case A_COMMAND:
        command(fd, req.strarg);
        break;
        case A_FILE:
        if(server->www_folder == NULL)
            send_error(fd, 501, "no www-folder configured");
        else
            send_file(fd, req.strurl);
        break;
        default:
        printf("unknown request\n");
        break;
    }
    printf("close fd\n");
    close(fd);
    printf("leaving HTTP client thread\n");
    return NULL;
}