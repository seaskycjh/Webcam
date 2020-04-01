#ifndef _OUTPUT_H_
#define _OUTPUT_H_

#include <pthread.h>

/* 服务器信息 */
typedef struct {
  int port;     //服务器端口
  char *www_folder;     //服务器文件夹
  int fd;  //服务器的socket描述符
  pthread_t threadID;  //服务器线程ID
}server_t;

/* 请求类型 */
typedef enum { A_UNKNOWN, A_SNAPSHOT, A_STREAM, A_COMMAND, A_FILE } answer_t;

/* 客户端请求消息 */
typedef struct {
  answer_t type;  //请求类型
  char strurl[64];  //请求资源链接
  char *strarg;  //请求参数
} request;

int output_init(void);
int output_run(void);
int output_stop(void);

#endif