#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "mjpg.h"
#include "httpd.h"

extern global_t global;

/* 读取客户端套接字 */
static int _read(int fd, char *buf, size_t len){
  char c = '\0';
  int rsize = 0, rc;
  struct timeval tv;
  fd_set fds;

  tv.tv_sec = 1;
  tv.tv_usec = 0;

  while(rsize < len){
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    rc = select(fd+1, &fds, NULL, NULL, &tv);
    if(rc < 0) return -1;
    else if(rc == 0) return rsize;
    else{
      if(FD_ISSET(fd, &fds)){
        if(recv(fd, &c, 1, 0) < 0){
          perror("recv failed:");
          break;
        }
        *buf++ = c;
        rsize++;
      }
    }
  }
  return rsize;
}

/* 读取一行数据 */
int _readline(int fd, void *buffer, size_t len){
    char c = '\0', *out = buffer;
    int i;
    for(i = 0; i < len && c != '\n'; i++){
        if(_read(fd, &c, 1) <= 0){
          return -1;
        }
        *out++ = c;
    }
    return i;
}

/* 发送单帧图片 */
void send_snapshot(int fd){
  unsigned char *frame=NULL;
  int frame_size=0;
  char buffer[BUFFER_SIZE] = {0};

  /* 等待新的帧到来 */
  pthread_cond_wait(&global.db_update, &global.db);

  /* 读取缓冲区 */
  frame_size = global.size;

  /* 为此单帧分配缓冲区 */
  if ( (frame = malloc(frame_size+1)) == NULL ) {
    free(frame);
    pthread_mutex_unlock( &global.db );
    send_error(fd, 500, "not enough memory");
    return;
  }

  memcpy(frame, global.buf, frame_size);
  printf("got frame (size: %d kB)\n", frame_size/1024);

  pthread_mutex_unlock( &global.db );

  /* write the response */
  sprintf(buffer, "HTTP/1.0 200 OK\r\n" \
                  STD_HEADER \
                  "Content-type: image/jpeg\r\n" \
                  "\r\n");

  /* 发送头部和图片 */
  if( write(fd, buffer, strlen(buffer)) < 0 ) {
    free(frame);
    return;
  }
  write(fd, frame, frame_size);

  free(frame);
}

/* 发送JPG帧流 */
void send_stream(int fd){
  unsigned char *frame=NULL, *tmp=NULL;
  int frame_size=0, max_frame_size=0;
  char buffer[BUFFER_SIZE] = {0};

  printf("preparing header\n");

  sprintf(buffer, "HTTP/1.0 200 OK\r\n" \
                  STD_HEADER \
                  "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n" \
                  "\r\n" \
                  "--" BOUNDARY "\r\n");

  if ( write(fd, buffer, strlen(buffer)) < 0 ) {
    free(frame);
    return;
  }

  printf("Headers send, sending stream now\n");

  while (!global.stop) {

    /* 等待新的帧到来 */
    pthread_cond_wait(&global.db_update, &global.db);

    /* 读取缓冲区 */
    frame_size = global.size;

    /* 检查帧缓冲区是否足够大，必要时增加它 */
#if 1 
    if (frame_size > max_frame_size) {
      printf("increasing buffer size to %d\n", frame_size);

      max_frame_size = frame_size+TEN_K;
      if ((tmp = realloc(frame, max_frame_size)) == NULL) {
        free(frame);
        pthread_mutex_unlock( &global.db );
        send_error(fd, 500, "not enough memory");
        return;
      }

      frame = tmp;
    }
		memcpy(frame, global.buf, frame_size);
		pthread_mutex_unlock( &global.db );
#endif
    
    sprintf(buffer, "Content-Type: image/jpeg\r\n" \
                    "Content-Length: %d\r\n" \
                    "\r\n", frame_size);
    /* 注意，当客户端连接断开时，继续向fd写数据会产生SIGPIPE信号，导致程序退出 */
    if(write(fd, buffer, strlen(buffer)) < 0) break;
    if(write(fd, frame, frame_size) < 0) break; //pglobal->buf
    sprintf(buffer, "\r\n--" BOUNDARY "\r\n");
    if(write(fd, buffer, strlen(buffer)) < 0) break;
  }
  printf("free frame\n");
  free(frame);
}

/***************************************************
描述: 发送错误信息及其头部
输入值: * fd.....: 客户端套接字
       * which..: HTTP错误码
       * message: 附加的错误信息
返回值: -
***************************************************/
void send_error(int fd, int which, char *message) {
  char buffer[BUFFER_SIZE] = {0};

  if ( which == 401 ) {
    sprintf(buffer, "HTTP/1.0 401 Unauthorized\r\n" \
                    "Content-type: text/plain\r\n" \
                    STD_HEADER \
                    "WWW-Authenticate: Basic realm=\"MJPG-Streamer\"\r\n" \
                    "\r\n" \
                    "401: Not Authenticated!\r\n" \
                    "%s", message);
  } else if ( which == 404 ) {
    sprintf(buffer, "HTTP/1.0 404 Not Found\r\n" \
                    "Content-type: text/plain\r\n" \
                    STD_HEADER \
                    "\r\n" \
                    "404: Not Found!\r\n" \
                    "%s", message);
  } else if ( which == 500 ) {
    sprintf(buffer, "HTTP/1.0 500 Internal Server Error\r\n" \
                    "Content-type: text/plain\r\n" \
                    STD_HEADER \
                    "\r\n" \
                    "500: Internal Server Error!\r\n" \
                    "%s", message);
  } else if ( which == 400 ) {
    sprintf(buffer, "HTTP/1.0 400 Bad Request\r\n" \
                    "Content-type: text/plain\r\n" \
                    STD_HEADER \
                    "\r\n" \
                    "400: Not Found!\r\n" \
                    "%s", message);
  } else {
    sprintf(buffer, "HTTP/1.0 501 Not Implemented\r\n" \
                    "Content-type: text/plain\r\n" \
                    STD_HEADER \
                    "\r\n" \
                    "501: Not Implemented!\r\n" \
                    "%s", message);
  }

  write(fd, buffer, strlen(buffer));
}

/* 发送文件 */
void send_file(int fd, char *parameter) {
  char buffer[BUFFER_SIZE] = {0};
  char *extension, *mimetype=NULL;
  int i, file_fd;

  /* 没有文件路径 */
  if ( parameter == NULL || strlen(parameter) == 0 )
    parameter = "index.html";

  /* 找出文件扩展名 */
  if ( (extension = strstr(parameter, ".")) == NULL ) {
    send_error(fd, 400, "No file extension found");
    return;
  }

  /* 确定文件类型对应的Conten-Type */
  for ( i=0; i < LENGTH_OF(mimetypes); i++ ) {
    if ( strcmp(mimetypes[i].dot_extension, extension) == 0 ) {
      mimetype = (char *)mimetypes[i].mimetype;
      break;
    }
  }

  /* 未知文件类型 */
  if ( mimetype == NULL ) {
    send_error(fd, 404, "MIME-TYPE not known");
    return;
  }

  printf("serve file: %s, extension: %s, mime: %s\n", parameter, extension, mimetype);

  /* 复制文件路径 */
  strcat(buffer, "www/");
  strcat(buffer, parameter);
  printf("%s\n", buffer);

  /* 打开文件 */
  if((file_fd = open(buffer, O_RDONLY)) < 0){
    printf("file %s not accessible\n", buffer);
    send_error(fd, 404, "Could not open file");
    return;
  }
  printf("opened file: %s\n", buffer);

  /* 准备头文件 */
  sprintf(buffer, "HTTP/1.0 200 OK\r\n" \
                  "Content-type: %s\r\n" \
                  STD_HEADER \
                  "\r\n", mimetype);
  i = strlen(buffer);

  /* 传输头文件和文件内容 */
  do{
    if(write(fd, buffer, i) < 0){
      close(file_fd);
      return;
    }
  }while((i=read(file_fd, buffer, sizeof(buffer))) > 0);

  /* 关闭打开的文件 */
  close(file_fd);
}


/* 处理命令并返回结果 */
void command(int fd, char *parameter) {
  char buffer[BUFFER_SIZE] = {0}, *command = NULL;
  char str1[24] = {0}, str2[24] = {0}, str3[24] = {0};
  int i=0, res=0, value=0;

  if(parameter == NULL || strlen(parameter) == 0){
    printf("command string error\n");
    send_error(fd, 400, "command not valid\n");
    return;
  }
  /* 将字符串以字符&分隔开 */
  sscanf(parameter, "%[^&]%*c%[^&]%*c%s", str1, str2, str3);
  if(strlen(str2) == 0){
    printf("no command specified\n");
    send_error(fd, 400, "No GET variable");
    return;
  }

  if((command = strchr(str2, '='))){
    command++;
  }

  if(strlen(str3) > 0){
    sscanf(str3, "value=%d", &value);
  }
  printf("%s: %d\n", command, value);
  /* 匹配命令字符串对应的命令编号 */

  /* 发送HTTP响应消息 */
  sprintf(buffer, "HTTP/1.0 200 OK\r\n" \
                  "Content-type: text/plain\r\n" \
                  STD_HEADER \
                  "\r\n" \
                  "%s: %d", command, res);

  write(fd, buffer, strlen(buffer));
}




