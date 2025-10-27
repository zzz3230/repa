#ifndef UTILS_H
#define UTILS_H
const char* generate_underline_string(int begin, int end);
char* copy_string(char* str, int* error);
int64_t parse_long(char* str, int* error);
double parse_double(char* str, int* error);
char** alloc_string_array(int count, int* error, ...);
long* alloc_integer_array(int count, int* error, ...);
#endif //UTILS_H
