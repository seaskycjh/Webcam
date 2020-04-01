#include <stdlib.h>
#include "v4l2uvc.h"

void print_fmt(int fmt){
	printf("%c%c%c%c\n", (fmt&0xFF), (fmt>>8)&0xFF, (fmt>>16)&0xFF, (fmt>>24)&0xFF);
}

int init_v4l2(struct vdIn *vd){
	int i;
	int ret = 0;

	if((vd->fd = open(vd->videodevice, O_RDWR)) < 0){
		perror("Open video fd");
		return -1;
	}

	memset(&vd->cap, 0, sizeof(struct v4l2_capability));
	ret = ioctl(vd->fd, VIDIOC_QUERYCAP, &vd->cap);
	if(ret < 0){
		printf("init_v4l2: unable to query device.\n");
		return -1;
	}

	if((vd->cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0){
		printf("init_v4l2: not support video capture.\n");
		return -1;
	}

	if(vd->grabmethod){
		if(!(vd->cap.capabilities & V4L2_CAP_STREAMING)){
			printf("init_v4l2: not support streaming i/o\n");
			return -1;
		}
	}else{
		if(!(vd->cap.capabilities & V4L2_CAP_READWRITE)){
			printf("init_v4l2: not support read i/o\n");
			return -1;
		}
	}

	//set format
	memset(&vd->fmt, 0, sizeof(struct v4l2_format));
	vd->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vd->fmt.fmt.pix.width = vd->width;
	vd->fmt.fmt.pix.height = vd->height;
	vd->fmt.fmt.pix.pixelformat = vd->format;
	vd->fmt.fmt.pix.field = V4L2_FIELD_ANY;
	ret = ioctl(vd->fd, VIDIOC_S_FMT, &vd->fmt);
	if(ret < 0){
		perror("init_v4l2: unable to set format");
		return -1;
	}

	if((vd->fmt.fmt.pix.width != vd->width) ||
				(vd->fmt.fmt.pix.height != vd->height)){
		vd->width = vd->fmt.fmt.pix.width;
		vd->height = vd->fmt.fmt.pix.height;
		printf("support width %d, height %d\n", vd->width, vd->height);
	}

	if(vd->fmt.fmt.pix.pixelformat != vd->format){
		vd->format = vd->fmt.fmt.pix.pixelformat;
		printf("support format: ");
		print_fmt(vd->format);
	}
	
	//set framerate
	struct v4l2_streamparm *setfps;
	setfps = (struct v4l2_streamparm *)malloc(sizeof(struct v4l2_streamparm));
	memset(setfps, 0, sizeof(struct v4l2_streamparm));
	setfps->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	setfps->parm.capture.timeperframe.numerator = 1;
	setfps->parm.capture.timeperframe.denominator = vd->fps;
	ret = ioctl(vd->fd, VIDIOC_S_PARM, setfps);

	//request buffers
	memset(&vd->rb, 0, sizeof(struct v4l2_requestbuffers));
	vd->rb.count = NB_BUFFER;
	vd->rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vd->rb.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(vd->fd, VIDIOC_REQBUFS, &vd->rb);
	if(ret < 0){
		perror("init_v4l2: unable to allocate buffers");
		return -1;
	}

	//map buffers
	for(i = 0; i < NB_BUFFER; i++){
		memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
		vd->buf.index = i;
		vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vd->buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(vd->fd, VIDIOC_QUERYBUF, &vd->buf);
		if(ret < 0){
			perror("init_v4l2: unable to query buffers");
			return -1;
		}

		printf("length: %u, offset: %u\n", vd->buf.length, vd->buf.m.offset);

		vd->mem[i] = mmap(0, vd->buf.length, PROT_READ,
						  MAP_SHARED, vd->fd, vd->buf.m.offset);
		if(vd->mem[i] == MAP_FAILED){
			perror("init_v4l2: unable to map buffers");
			return -1;
		}
		printf("buffer mapped at address %p.\n", vd->mem[i]);
	}

	//queue the buffers
	for(i = 0; i < NB_BUFFER; i++){
		memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
		vd->buf.index = i;
		vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		vd->buf.memory = V4L2_MEMORY_MMAP;
		ret = ioctl(vd->fd, VIDIOC_QBUF, &vd->buf);
		if(ret < 0){
			perror("init_v4l2: unable to queue buffers");
			return 0;
		}
	}

	return 0;
}

int v4l2_enable(struct vdIn *vd){
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;

	ret = ioctl(vd->fd, VIDIOC_STREAMON, &type);
	if(ret < 0){
		perror("video_enable: unable to start capture");
		return -1;
	}
	vd->isstreaming = 1;
	return 0;
}

int v4l2_disable(struct vdIn *vd){
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	int ret;
	
	ret = ioctl(vd->fd, VIDIOC_STREAMOFF, &type);
	if(ret < 0){
		perror("video_enable: unable to stop capture");
		return -1;
	}
	vd->isstreaming = 0;
	return 0;
}

int v4l2_grab(struct vdIn *vd){
#define HEADERFRAME1 0xaf
	int ret;
	
	if(!vd->isstreaming)
	  if(v4l2_enable(vd)) goto err;

	memset(&vd->buf, 0, sizeof(struct v4l2_buffer));
	vd->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	vd->buf.memory = V4L2_MEMORY_MMAP;
	ret = ioctl(vd->fd, VIDIOC_DQBUF, &vd->buf);
	if(ret < 0){
		perror("unable to dequeue buffers");
		goto err;
	}

	//取出YUYV图像数据
	//if(vd->buf.bytesused > vd->yuyv_size)
	  //memcpy(vd->yuyv_buf, vd->mem[vd->buf.index], (size_t)vd->yuyv_size);
	//else
	  //memcpy(vd->yuyv_buf, vd->mem[vd->buf.index], (size_t)vd->buf.bytesused);

	if(vd->buf.bytesused > vd->jpeg_size)
		memcpy(vd->jpeg_buf, vd->mem[vd->buf.index], (size_t)vd->jpeg_size);
	else
		memcpy(vd->jpeg_buf, vd->mem[vd->buf.index], (size_t)vd->buf.bytesused);
	
	//缓冲重新入列
	ret = ioctl(vd->fd, VIDIOC_QBUF, &vd->buf);
	if(ret < 0){
		perror("unable to requeue buffers");
		goto err;
	}

	return 0;

err:
	vd->signalquit = 0;
	return -1;
}

int close_v4l2(struct vdIn *vd){
	if(vd->isstreaming) v4l2_disable(vd);
	if(vd->rgb_buf){
		free(vd->rgb_buf);
		vd->rgb_buf = NULL;
	}
	free(vd->yuyv_buf);
	vd->yuyv_buf = NULL;
	free(vd->jpeg_buf);
	vd->jpeg_buf = NULL;
	free(vd->videodevice);
	vd->videodevice = NULL;
	return 0;
}









