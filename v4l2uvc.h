#ifndef _V4L2UVC_H_
#define _V4L2UVC_H_

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

#define NB_BUFFER 4

struct vdIn {
    int fd;
    char *videodevice;
    int isstreaming;
    int grabmethod;
    int width;
    int height;
    int fps;
    int format;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers rb;
    void *mem[NB_BUFFER];
    unsigned char *rgb_buf;
    unsigned char *yuyv_buf;
    unsigned char *jpeg_buf;
    int yuyv_size;
    int rgb_size;
    int jpeg_size;
    int signalquit;
    /* raw stream capture */
    FILE *captureFile;
};

void print_fmt(int fmt);
int init_v4l2(struct vdIn *vd);
int v4l2_grab(struct vdIn *vd);
int close_v4l2(struct vdIn *vd);

#endif


