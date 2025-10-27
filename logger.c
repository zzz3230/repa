#include <stdbool.h>
#include "stdio.h"
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "logger.h"

bool was_init = false;

char* l_path;
int l_file_size_limit;
FILE* streams_table[EOF_OUTPUT_STREAM_ENUM];

pthread_mutex_t log_mutex;


int init_logger(char* path, int file_size_limit){
    if(was_init){
        return 1;
    }
    if(file_size_limit < -1){
        return 1;
    }

    if(path == NULL){
        was_init = true;
    }

    pthread_mutex_init(&log_mutex, NULL);

    was_init = true;

    l_path = path;
    l_file_size_limit = file_size_limit;


    streams_table[STDOUT] = stdout;
    streams_table[STDERR] = stderr;
    streams_table[FILESTREAM] = fopen(path, "a+"); //TODO: check if file exists and is writable

    return 0;
}

int fini_logger(void){
    if(!was_init){
        return 1;
    }
    was_init = false;

    return 0;
}

char* loglevel_to_string(LogLevel level){
    switch (level) {
        case LOG_DEBUG:
            return "DEBUG";
        case LOG_INFO:
            return "INFO";
        case LOG_WARNING:
            return "WARNING";
        case LOG_ERROR:
            return "ERROR";
        case LOG_FATA:
            return "FATA";
    }
    return "UNKNOWN_LEVEL";
}

int format_tm(struct tm *tm_info, char *buffer, size_t buffer_size) {
    int len = 0;
    len += (int)strftime(buffer, buffer_size, "%Y-%m-%dT%H:%M:%S", tm_info);

    char time_zone[10];
    len += (int)strftime(time_zone, sizeof(time_zone), "%Z", tm_info);

    len += snprintf(buffer + strlen(buffer), buffer_size - strlen(buffer), " (%s)", time_zone);

    return len;
}



int write_log(OutputStream stream, LogLevel level, char *filename, int line_number, char* format, ...) {
    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&log_mutex);


    pid_t pid = getpid();

    time_t now_time = time(NULL);
    struct tm * time_info;
    time_info = localtime (&now_time);

    if (time_info == NULL) {
        fprintf(stderr, "Failed to get local time\n");
        return 1;
    }

    char time_buffer[32] = {0};
    format_tm(time_info, time_buffer, sizeof(time_buffer));


    for (int i = 0; i < sizeof(streams_table) / sizeof(streams_table[0]); ++i) {
        if(true == true){
            if(i != STDOUT){
                continue;
            }
        }
        else{
            if(stream != i){
                continue;
            }
        }


        fprintf(streams_table[i], "%s ", time_buffer);
        fprintf(streams_table[i], "%s:%d [%d] | %s: ", filename, line_number, pid, loglevel_to_string(level));
        vfprintf(streams_table[i], format, args);
        fprintf(streams_table[i], "\n");

        fflush(streams_table[i]);
    }

    pthread_mutex_unlock(&log_mutex);

    va_end(args);
    return 0;
}
