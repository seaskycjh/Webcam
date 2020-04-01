#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>

#include "mjpg.h"
//#include "simplified_jpeg_encoder.h"
#include "v4l2uvc.h"
#include "jpeglib.h"

typedef unsigned char uint_8;

pthread_t cam;
struct vdIn *videoIn;
extern global_t global;
static int gquality = 80;
static unsigned int minimum_size = 0;

void *cam_thread(void *arg);
void cam_cleanup(void *arg);
int yuyv_2_rgb(unsigned char *yuyv, unsigned char *rgb, int width, int height);
int rgb_2_jpeg(unsigned char *rgb_buf, unsigned char **jpeg_buf, int width, int height, int quality);
int yuyv_2_jpeg(unsigned char *yuyv_buf, unsigned char **jpeg_buf, int width, int height, int quality);

static void read_arg(char *buf){
	char *arg = NULL;
	int len = strlen(buf);
	int val = 0;
	if((arg = strstr(buf, "device"))){
		arg += 7;
		videoIn->videodevice = malloc(len+1); //TODO
		strcpy(videoIn->videodevice, arg);
	}else if((arg = strstr(buf, "width"))){
		arg += 6;
		if(sscanf(arg, "%d", &val)) videoIn->width = val;
	}else if((arg = strstr(buf, "height"))){
		arg += 7;
		if(sscanf(arg, "%d", &val)) videoIn->height = val;
	}else if((arg = strstr(buf, "fps"))){
		arg += 4;
		if(sscanf(arg, "%d", &val)) videoIn->fps = val;
	}else if((arg = strstr(buf, "format"))){
		arg += 7;
		if(strstr(arg, "YUYV")) videoIn->format = V4L2_PIX_FMT_YUYV;
		if(strstr(arg, "MJPG")) videoIn->format = V4L2_PIX_FMT_MJPEG;
	}
}
		
//支持MJPG或YUYV格式
int input_init(void){
    int conf_fd, i = 0;
    char buf[64] = {0}, c;

    videoIn = (struct vdIn *)malloc(sizeof(struct vdIn));
    if(videoIn == NULL){
        perror("malloc");
        exit(1);
    }
    memset(videoIn, 0, sizeof(struct vdIn));

    conf_fd = open("mjpg.conf", O_RDONLY, 0);
    if(conf_fd < 0){
        perror("open conf");
        exit(1);
    }
	//读取设备参数
	while(read(conf_fd, &c, 1)){
		if(c == '\r' || c == '\n'){
			buf[i] = '\0';
			//printf("%s\n", buf);
			read_arg(buf);
			i = 0;
			continue;
		}
		buf[i++] = c;
	}
	printf("Device: %s\n", videoIn->videodevice);
	printf("Width: %d, Height: %d\n", videoIn->width, videoIn->height);
	printf("Fps: %d\n", videoIn->fps);
	printf("Format: ");
	print_fmt(videoIn->format);

	videoIn->signalquit = 1;
	videoIn->grabmethod = 1;

	if(init_v4l2(videoIn) < 0){
		printf("init video failed!\n");
		exit(1);
	}

	if(videoIn->format == V4L2_PIX_FMT_YUYV){
		videoIn->yuyv_size = (videoIn->width * videoIn->height << 1);
		videoIn->yuyv_buf = (unsigned char *)malloc((size_t)videoIn->yuyv_size);
		if(videoIn->yuyv_buf == NULL) return -1;

		videoIn->rgb_size = videoIn->width * videoIn->height * 3;
		videoIn->rgb_buf = (unsigned char *)malloc((size_t)videoIn->rgb_size);
		if(videoIn->rgb_buf == NULL) return -1;
	}
	
	videoIn->jpeg_size = videoIn->width * videoIn->height;
	videoIn->jpeg_buf = (unsigned char *)malloc((size_t)videoIn->jpeg_size);
	if(videoIn->jpeg_buf == NULL) return -1;

	return 0;
}

int input_run(void){
	printf("framesize: %d", videoIn->jpeg_size);
	
	global.buf = malloc(videoIn->jpeg_size);
	if(global.buf == NULL){
		printf("could not allocate memory\n");
		exit(-1);
	}

	pthread_create(&cam, 0, cam_thread, NULL);
	pthread_detach(cam);
	
	return 0;
}

int input_stop(void){
	pthread_cancel(cam);
	return 0;
}

void cam_cleanup(void *arg){
	static unsigned char first_run = 1;

	if(!first_run){
		printf("already cleaned up res\n");
		return;
	}

	first_run = 0;
	printf("cleaning up res allocated by input thread\n");

	close_v4l2(videoIn);
	if(videoIn != NULL) free(videoIn);
	if(global.buf != NULL) free(global.buf);
}

void *cam_thread(void *arg){
	pthread_cleanup_push(cam_cleanup, NULL);

	while(!global.stop){
		if(v4l2_grab(videoIn) < 0){
			printf("error grabbing frames\n");
			exit(-1);
		}
		//printf("recv frame of size: %d\n", videoIn->buf.bytesused);

		if(videoIn->buf.bytesused < minimum_size){
			printf("too small frame\n");
			continue;
		}

		//memset(videoIn->rgb_buf, 0, videoIn->rgb_size);
		//yuyv_2_rgb(videoIn->yuyv_buf, videoIn->rgb_buf, videoIn->width, videoIn->height);

		pthread_mutex_lock(&global.db);
		//将YUYV格式压缩为JPEG格式
		//videoIn->jpeg_size = rgb_2_jpeg(videoIn->rgb_buf, &videoIn->jpeg_buf, videoIn->width, videoIn->height, 50);
		/*
		videoIn->jpeg_size = yuyv_2_jpeg(videoIn->yuyv_buf, &videoIn->jpeg_buf, videoIn->width, videoIn->height, 50);
		if(videoIn->jpeg_buf){
			memcpy(global.buf, videoIn->jpeg_buf, videoIn->jpeg_size);
			global.size = videoIn->jpeg_size;
			free(videoIn->jpeg_buf);
			videoIn->jpeg_buf = NULL;
		}*/
		memcpy(global.buf, videoIn->jpeg_buf, (size_t)videoIn->jpeg_size);
		global.size = videoIn->jpeg_size;

		pthread_cond_broadcast(&global.db_update);
		pthread_mutex_unlock(&global.db);

		if(videoIn->fps < 5) usleep(1000*1000/videoIn->fps);
	}

	pthread_cleanup_pop(1);
	return NULL;
}

int yuyv_2_rgb(unsigned char *yuyv, unsigned char *rgb, int width, int height){
	unsigned int i;
	unsigned char *y0 = yuyv + 0;
	unsigned char *u0 = yuyv + 1;
	unsigned char *y1 = yuyv + 2;
	unsigned char *v0 = yuyv + 3;

	unsigned char *r0 = rgb + 0;
	unsigned char *g0 = rgb + 1;
	unsigned char *b0 = rgb + 2;
	unsigned char *r1 = rgb + 3;
	unsigned char *g1 = rgb + 4;
	unsigned char *b1 = rgb + 5;

	float rt0 = 0, gt0 = 0, bt0 = 0, rt1 = 0, gt1 = 0, bt1 = 0;
	for(i = 0; i <= (width * height) / 2; i++){
		bt0 = 1.164 * (*y0 - 16) + 2.018 * (*u0 - 128);
		gt0 = 1.164 * (*y0 - 16) + 0.813 * (*v0 - 128) - 0.394 * (*u0 - 128);
		rt0 = 1.164 * (*y0 - 16) + 1.596 * (*v0 - 128);

		bt1 = 1.164 * (*y1 - 16) + 2.018 * (*u0 - 128);
		gt1 = 1.164 * (*y1 - 16) + 0.813 * (*v0 - 128) - 0.394 * (*u0 - 128);
		rt1 = 1.164 * (*y1 - 16) + 1.596 * (*v0 - 128);

		if(rt0 > 250) rt0 = 255;
		if(rt0 < 0) rt0 = 0;
		if(gt0 > 250) gt0 = 255;
		if(gt0 < 0) gt0 = 0;
		if(bt0 > 250) bt0 = 255;
		if(bt0 < 0) bt0 = 0;
		if(rt1 > 250) rt1 = 255;
		if(rt1 < 0) rt1 = 0;
		if(gt1 > 250) gt1 = 255;
		if(gt1 < 0) gt1 = 0;
		if(bt1 > 250) bt1 = 255;
		if(bt1 < 0) bt1 = 0;

		*r0 = (unsigned char)rt0;
		*g0 = (unsigned char)gt0;
		*b0 = (unsigned char)bt0;
		*r1 = (unsigned char)rt1;
		*g1 = (unsigned char)gt1;
		*b1 = (unsigned char)bt1;

		yuyv = yuyv + 4;
		rgb = rgb + 6;
		if(yuyv == NULL) break;

		y0 = yuyv + 0;
		u0 = yuyv + 1;
		y1 = yuyv + 2;
		v0 = yuyv + 3;

		r0 = rgb + 0;
		g0 = rgb + 1;
		b0 = rgb + 2;
		r1 = rgb + 3;
		g1 = rgb + 4;
		b1 = rgb + 5;
	}
	return 0;
}

int rgb_2_jpeg(unsigned char *rgb_buf, unsigned char **jpeg_buf, int width, int height, int quality){
	int jpeg_size;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int row_stride = 0;
	JSAMPROW row_pointer[1];

	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, jpeg_buf, &jpeg_size);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, 1);
	jpeg_start_compress(&cinfo, TRUE);
	row_stride = width * cinfo.input_components;

	while(cinfo.next_scanline < cinfo.image_height){
		row_pointer[0] = &rgb_buf[cinfo.next_scanline * row_stride];
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	
	return jpeg_size;
}

int yuyv_2_jpeg(unsigned char *yuyv_buf, unsigned char **jpeg_buf, int width, int height, int quality){
	int jpeg_size = 0, z = 0;
	uint_8 *line_buf, *yuyv;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	int row_stride = 0;
	JSAMPROW row_pointer[1];

	line_buf = (uint_8 *)malloc(width * 3);
	yuyv = yuyv_buf;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, jpeg_buf, &jpeg_size);
	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	while(cinfo.next_scanline < cinfo.image_height){
		int i;
		uint_8 *ptr = line_buf;
		for(i = 0; i < width; i++){
			float r, g, b;
			int y, u, v;
			if(z == 0) y = yuyv[0] - 16;
			else y = yuyv[2] - 16;
			u = yuyv[1] - 128;
			v = yuyv[3] - 128;

			r = 1.164 * y + 2.018 * v;
			g = 1.164 * y + 0.813 * v - 0.394 * u;
			b = 1.164 * y + 1.596 * v;

			*(ptr++) = (uint_8)((r>255)?255:((r<0)?0:r));
			*(ptr++) = (uint_8)((g>255)?255:((g<0)?0:g));
			*(ptr++) = (uint_8)((b>255)?255:((b<0)?0:b));

			if(z++){
				z = 0;
				yuyv += 4;
			}
		}
		row_pointer[0] = line_buf;
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	free(line_buf);
	
	return jpeg_size;
}







