//
// Created by zzz32 on 3/13/2025.
//
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include "exception.h"

#define EXCEPTION_BUFFER_SIZE 512
char exception_buffer[4][EXCEPTION_BUFFER_SIZE];

char pop_exception_buffer[4 * EXCEPTION_BUFFER_SIZE];

int exceptions_count = 0;

bool has_exception(void) {
    return exceptions_count > 0;
}

// save exception to the buffer
void push_exception(char *filename, int line, const char* func, const char *format, ...){
    va_list args;
    va_start(args, format);

    char* current_buffer = exception_buffer[exceptions_count];

    int written1 = sprintf(current_buffer, "at %s() %s:%d: ", func, filename, line);

    int written2 = vsnprintf(current_buffer + written1, EXCEPTION_BUFFER_SIZE - written1, format, args);

    sprintf(current_buffer + written1 + written2, "\n");



    va_end(args);
    exceptions_count++;
}

void print_exceptions_trace(void) {
    for (int i = 0; i < exceptions_count; ++i) {
        fprintf(stderr, "%s", exception_buffer[i]);
    }
}

char* pop_exception_trace(void){
    char* buffer = pop_exception_buffer;
    int offset = 0;
    for (int i = 0; i < exceptions_count; ++i) {
        offset += sprintf(buffer + offset, "%s", exception_buffer[i]);
    }
    exceptions_count = 0;
    return buffer;
}
