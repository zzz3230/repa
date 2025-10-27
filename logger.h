#ifndef LOGGER_H
#define LOGGER_H
#define SHORT_FILE (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define LOG_(level, format, ...)
#define LOG(level, format, ...) write_log(STDOUT, level, SHORT_FILE, __LINE__, format, ##__VA_ARGS__)
#define LOG_EXCEPTION_STACK() write_log(STDERR, LOG_ERROR, __FILE__, __LINE__, "Exception stack trace:\n%s", pop_exception_trace())

typedef enum LogLevel{
    LOG_DEBUG = 1,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATA
} LogLevel;

typedef enum OutputStream{
    STDOUT = 1,
    STDERR,
    FILESTREAM,
    EOF_OUTPUT_STREAM_ENUM
} OutputStream;

int init_logger(char* path, int file_size_limit);
int fini_logger(void);


int write_log(OutputStream stream, LogLevel level, char* filename, int line_number, char* format, ...) __attribute__ ((format(printf, 5, 0)));

#endif //LOGGER_H
