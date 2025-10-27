#include <malloc.h>
#include <stdlib.h>
#include <stdarg.h>
#include "utils.h"
#include "string.h"
#include "exception.h"

const char* generate_underline_string(int begin, int end) {
    static char buffer[1024]; // Статический буфер
    int length = end - begin;

    if (length < 0 || end >= sizeof(buffer)) {
        return "Error: invalid range";
    }

    memset(buffer, ' ', begin);   // Пробелы до `begin`
    memset(buffer + begin, '^', length); // `^` от `begin` до `end`
    buffer[end] = '\0'; // Завершаем строку

    return buffer;
}

char* copy_string(char* str, int* error) {
    size_t len = strlen(str);
    char* new_str = malloc(len + 1);
    if (new_str == NULL) {
        if(error != NULL){
            *error = 1;
        }

        PUSH_EXCEPTION("Failed to allocate memory for string with length %ld", len);
        return NULL;
    }
    strcpy(new_str, str);
    return new_str;
}

int64_t parse_long(char* str, int* error){
    char* end;
    long result = strtol(str, &end, 10);
    if(*end != '\0'){
        *error = 1;
        PUSH_EXCEPTION("Failed to parse integer from string: %s", str);
        return 0;
    }
    return result;
}

double parse_double(char* str, int* error){
    char* end;
    double result = strtod(str, &end);
    if(*end != '\0'){
        *error = 1;
        PUSH_EXCEPTION("Failed to parse double from string: %s", str);
        return 0;
    }
    return result;
}


char** alloc_string_array(int count, int* error, ...){
    va_list args;
    va_start(args, error);

    char** array = malloc(count * sizeof(char*));
    if (array == NULL) {
        if(error != NULL){
            *error = 1;
        }
        PUSH_EXCEPTION("Failed to allocate memory for string array of size %d", count);
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        char* str = va_arg(args, char*);
        array[i] = str;
    }

    va_end(args);
    return array;
}

int64_t* alloc_integer_array(int count, int* error, ...){
    va_list args;
    va_start(args, error);

    long* array = malloc(count * sizeof(int64_t));
    if (array == NULL) {
        if(error != NULL){
            *error = 1;
        }
        PUSH_EXCEPTION("Failed to allocate memory for integer array of size %d", count);
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        int64_t integer = va_arg(args, int64_t);
        array[i] = integer;
    }

    va_end(args);
    return array;
}
