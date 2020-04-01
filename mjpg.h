#ifndef _MJPG_H_
#define _MJPG_H_

#include <pthread.h>

#define ABS(a) (((a) < 0) ? -(a) : (a))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define LENGTH_OF(x) (sizeof(x)/sizeof(x[0]))


typedef struct _global{
  int stop;

  pthread_mutex_t db;
  pthread_cond_t  db_update;

  unsigned char *buf;
  int size;

}global_t;

#endif
