#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H
#include "config_types.h"

enum TokenType{
    TOKEN_TYPE_UNDEFINED = 0,
    TOKEN_TYPE_INTEGER = 1,
    TOKEN_TYPE_REAL,
    TOKEN_TYPE_STRING,
    TOKEN_TYPE_IDENTIFIER,
    TOKEN_TYPE_ASSIGNMENT,
    TOKEN_TYPE_LEFT_BRACKET,
    TOKEN_TYPE_RIGHT_BRACKET,
    TOKEN_TYPE_LEFT_QUOTE,
    TOKEN_TYPE_RIGHT_QUOTE,
    TOKEN_TYPE_COMMA,
    TOKEN_TYPE_EOF
};

typedef struct Token{
    enum TokenType type;
    char* value;
    int source_begin;
    int source_end;
} Token;

//int config_parser_push_char(char c);
//void print_tokens(void);
//int config_parser_finish(void);
int config_parser_parse_file(const char* path);
void config_parser_get_parsed_vars(ConfigVariable** out_vars, int* out_count);
void config_parser_finish_parsing_file(void);
void print_variable(ConfigVariable var);
void finish_parsing(void);
#endif //CONFIG_PARSER_H
