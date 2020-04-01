#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "mjpg.h"
#include "input.h"
#include "output.h"

global_t global;

static void daemon_mode(void){
    int i;
    int pid = fork();
    if(pid < 0){
        fprintf(stderr, "fork fail\n");
        exit(1);
    }
    if(pid > 0){
        exit(0);
    }
    if(setsid() < 0){
        fprintf(stderr, "setsid fail\n");
        exit(1);
    }
    umask(0);
    chdir("/");
    for(i = 0; i < 3; i++)
        close(i);
}

void signal_handler(int sig){
	global.stop = 1;
	usleep(1000*1000);

	input_stop();
	pthread_cond_destroy(&global.db_update);
	pthread_mutex_destroy(&global.db);

	exit(0);
}

int main(int argc, char const *argv[])
{
    //pthread_t server_id;
    //daemon_mode();
    //openlog("mjpg-streamer", LOG_PID|LOG_CONS, LOG_USER);
    //syslog(LOG_INFO, "start application");

    global.stop = 0;
    global.buf = NULL;
    global.size = 0;

    if(pthread_mutex_init(&global.db, NULL)){
        printf("could not init mutex variable\n");
        exit(1);
    }
    if(pthread_cond_init(&global.db_update, NULL)){
        printf("could not init condition variable\n");
        exit(1);
    }

    //忽略SIGPIPE信号，当客户端关闭套接字时，若继续写套接字则会产生SIGPIPE信
	//导致程序退出
    signal(SIGPIPE, SIG_IGN);
	//register handler for SIGINT(<CTRL>+C) to clean up
	if(signal(SIGINT, signal_handler) == SIG_ERR){
		printf("could not register signal handler\n");
		exit(-1);
	}

    printf("start input func\n");
	input_init();
	input_run();

    printf("start output func\n");
    output_init();
    output_run();

    /* 等待信号 */
    pause();
    return 0;
}
