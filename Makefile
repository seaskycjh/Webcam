CC = arm-none-linux-gnueabi-gcc-4.4.1

CFLAGS += -Wall -O3
LDFLAGS := -lpthread -ldl -ljpeg

APP_BIN = mjpg
OBJS = mjpg.c input.c v4l2uvc.c output.c httpd.c

$(APP_BIN): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(APP_BIN)

clean:
	rm -f *.a *.o $(APP_BINARY)