#define BUFFER_SIZE 1024

/* the boundary is used for the M-JPEG stream, it separates the multipart stream of pictures */
#define BOUNDARY "boundarydonotcross"

#define MAX_FRAME_SIZE (256*1024)
#define TEN_K (10*1024)

/* 标准响应头部 */
#define STD_HEADER "Connection: close\r\n" \
                   "Server: MJPG-Streamer/0.2\r\n" \
                   "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
                   "Pragma: no-cache\r\n" \
                   "Expires: Mon, 3 Aug 2019 12:34:56 GMT\r\n"

/* 支持的文件格式 */
static const struct {
  const char *dot_extension;
  const char *mimetype;
} mimetypes[] = {
  { ".html", "text/html" },
  { ".htm",  "text/html" },
  { ".css",  "text/css" },
  { ".js",   "text/javascript" },
  { ".txt",  "text/plain" },
  { ".jpg",  "image/jpeg" },
  { ".jpeg", "image/jpeg" },
  { ".png",  "image/png"},
  { ".gif",  "image/gif" },
  { ".ico",  "image/x-icon" },
  { ".swf",  "application/x-shockwave-flash" },
  { ".cab",  "application/x-shockwave-flash" },
  { ".jar",  "application/java-archive" }
};


int _readline(int fd, void *buffer, size_t len);
void send_snapshot(int fd);
void send_stream(int fd);
void send_file(int fd, char *parameter);
void command(int fd, char *parameter);
void send_error(int fd, int which, char *message);




