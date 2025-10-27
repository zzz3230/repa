#ifndef EXCEPTION_H
#define EXCEPTION_H
#include <stdbool.h>


#define PUSH_EXCEPTION(format, ...) push_exception(__FILE__, __LINE__, __func__, format, ##__VA_ARGS__)

void push_exception(char* filename, int line, const char* func, const char* format, ...) __attribute__ ((format(printf, 4, 0)));
bool has_exception(void);
void print_exceptions_trace(void);
char* pop_exception_trace(void);

#endif //EXCEPTION_H
