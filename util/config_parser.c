#include <stddef.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "config_parser.h"

#include <errno.h>
#include "../logger.h"

#include "exception.h"
#include "utils.h"
#include "config.h"

void print_variable(ConfigVariable var){
    printf("Variable: %s [%s]\n", var.name, var.description);
    for (int i = 0; i < var.count; ++i) {
        if(var.type == INTEGER){
            printf("  %ld\n", var.data.integer[i]);
        }
        else if(var.type == REAL){
            printf("  %f\n", var.data.real[i]);
        }
        else if(var.type == STRING){
            printf("  %s\n", var.data.string[i]);
        }
    }
}

#define BUFFER_SIZE 1024

char static_tokens[16] = {
        [TOKEN_TYPE_ASSIGNMENT] = '=',
        [TOKEN_TYPE_LEFT_BRACKET] = '[',
        [TOKEN_TYPE_RIGHT_BRACKET] = ']',
        [TOKEN_TYPE_LEFT_QUOTE] = '"',
        [TOKEN_TYPE_RIGHT_QUOTE] = '"',
        [TOKEN_TYPE_COMMA] = ','
};

Token* tokens = NULL;
int tokens_count = 0;
int tokens_capacity = 0;

int token_begin_at_line = 0;

bool is_in_token = false;
enum TokenType current_continuous_token_type = TOKEN_TYPE_UNDEFINED;

char current_token_buffer[BUFFER_SIZE];
int current_token_buffer_index = 0;

char last_line[BUFFER_SIZE];
int last_line_index = 0;

ConfigVariable* parsed_vars = NULL;
int parsed_vars_count = 0;
int parsed_vars_capacity = 0;


int safe_realloc(void** ptr, size_t size) {
    void* new_ptr = realloc(*ptr, size);
    if (new_ptr == NULL) {
        PUSH_EXCEPTION("Failed to reallocate memory (new size %ld)", size);
        return 1;
    }
    *ptr = new_ptr;
    return 0;
}

// ### TOKEN PARSING

void finalize_token(enum TokenType type){
    current_token_buffer[current_token_buffer_index] = '\0';
    //if(type == TOKEN_TYPE_INTEGER){
        //printf("%s at %d\n", current_token_buffer, tokens_count);
    //}
    tokens[tokens_count].value = malloc(current_token_buffer_index + 1);
    if (tokens[tokens_count].value == NULL) {
        PUSH_EXCEPTION("Failed to allocate memory for token value");
        return;
    }

    strcpy(tokens[tokens_count].value, current_token_buffer);

    tokens[tokens_count].type = type;
    tokens[tokens_count].source_begin = token_begin_at_line;
    tokens[tokens_count].source_end = last_line_index - 1;
    tokens_count++;
    is_in_token = false;
    current_token_buffer_index = 0;

    current_continuous_token_type = TOKEN_TYPE_UNDEFINED;
}

int resize_tokens_if_need(void){
    if (tokens_count + 2 >= tokens_capacity) {
        int new_capacity = (tokens_capacity + 2) * 2;
        if (safe_realloc((void**)&tokens, new_capacity * sizeof(Token)) == 1) {
            PUSH_EXCEPTION("Failed to reallocate tokens (new size %d)", new_capacity);
            return 1;
        }
        tokens_capacity = new_capacity;
    }
    return 0;
}

int resize_parsed_vars_if_need(void){
    if (parsed_vars_count + 2 >= parsed_vars_capacity) {
        int new_capacity = (parsed_vars_capacity + 2) * 2;
        if (safe_realloc((void**)&parsed_vars, new_capacity * sizeof(ConfigVariable)) == 1) {
            PUSH_EXCEPTION("Failed to reallocate parsed_vars (new size %d)", new_capacity);
            return 1;
        }
        parsed_vars_capacity = new_capacity;
    }
    return 0;
}

#define APPEND_CHAR_TO_TOKEN_BUFFER(ch) if(1) { \
 current_token_buffer[current_token_buffer_index++] = ch; \
 if(current_token_buffer_index+1 >= BUFFER_SIZE){      \
      PUSH_EXCEPTION("Token buffer size reached (%d)", BUFFER_SIZE); \
      return 1;\
 }}

int config_parser_push_char(const char c) {
    if (resize_tokens_if_need() == 1) {
        return 1;
    }

    if (isspace(c)) {
        if (!is_in_token) {
            return 0;
        } else {
            if (current_continuous_token_type != TOKEN_TYPE_STRING) {
                finalize_token(current_continuous_token_type);
                return 0;
            }
        }
    }

    if (current_continuous_token_type == TOKEN_TYPE_STRING) {
        if (c != static_tokens[TOKEN_TYPE_RIGHT_QUOTE] && is_in_token) {
            APPEND_CHAR_TO_TOKEN_BUFFER(c);
            return 0;
        } else {
            finalize_token(TOKEN_TYPE_STRING);
            return 0; // Don't add the quote to the token
        }
    }

    if (current_continuous_token_type == TOKEN_TYPE_INTEGER) {
        if (isdigit(c) && is_in_token) {
            APPEND_CHAR_TO_TOKEN_BUFFER(c);
            return 0;
        } else if (c == '.' && is_in_token) {
            current_continuous_token_type = TOKEN_TYPE_REAL;
            APPEND_CHAR_TO_TOKEN_BUFFER(c);
            return 0;
        } else {
            finalize_token(TOKEN_TYPE_INTEGER);
        }
    }

    if (current_continuous_token_type == TOKEN_TYPE_REAL) {
        if (isdigit(c) && is_in_token) {
            APPEND_CHAR_TO_TOKEN_BUFFER(c);
            return 0;
        } else {
            finalize_token(TOKEN_TYPE_REAL);
        }
    }

    if (current_continuous_token_type == TOKEN_TYPE_IDENTIFIER) {
        if ((isalnum(c) || c == '_') && is_in_token) {
            APPEND_CHAR_TO_TOKEN_BUFFER(c);
            return 0;
        } else {
            finalize_token(TOKEN_TYPE_IDENTIFIER);
        }
    }

    for (int i = 0; i < 16; ++i) {
        if (static_tokens[i] == c) {

            if (c == static_tokens[TOKEN_TYPE_LEFT_QUOTE]) {
                current_continuous_token_type = TOKEN_TYPE_STRING;
                token_begin_at_line = last_line_index - 1;
                is_in_token = true;
                return 0; // Don't add the quote to the token
            } else {
                current_continuous_token_type = TOKEN_TYPE_UNDEFINED;
            }

            tokens[tokens_count].type = i;
            tokens[tokens_count].source_begin = last_line_index - 1;
            tokens[tokens_count].source_end = last_line_index;
            tokens[tokens_count].value = NULL;
            tokens_count++;

            return 0;
        }
    }

    if (isdigit(c) || c == '-') {
        if (!is_in_token) {
            is_in_token = true;
            token_begin_at_line = last_line_index - 1;
            current_continuous_token_type = TOKEN_TYPE_INTEGER;
        }
        APPEND_CHAR_TO_TOKEN_BUFFER(c);
        return 0;
    }

    if (isalpha(c) || c == '_') {
        if (!is_in_token) {
            is_in_token = true;
            token_begin_at_line = last_line_index - 1;
            current_continuous_token_type = TOKEN_TYPE_IDENTIFIER;
        }
        APPEND_CHAR_TO_TOKEN_BUFFER(c);
        return 0;
    }

    PUSH_EXCEPTION("Unexpected character: '%c'", c);

    return 1;
}

int config_parser_finish(void){
    if (resize_tokens_if_need() == 1) {
        return 1;
    }

    if(current_continuous_token_type != TOKEN_TYPE_UNDEFINED){
        if(current_continuous_token_type == TOKEN_TYPE_STRING){
            PUSH_EXCEPTION("Unexpected end of string");
            return 1;
        }
        finalize_token(current_continuous_token_type);
    }

    return 0;
}

char* token_type_to_string(enum TokenType type){
    switch (type) {
        case TOKEN_TYPE_UNDEFINED:
            return "UNDEFINED";
        case TOKEN_TYPE_INTEGER:
            return "INTEGER";
        case TOKEN_TYPE_REAL:
            return "REAL";
        case TOKEN_TYPE_STRING:
            return "STRING";
        case TOKEN_TYPE_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_TYPE_ASSIGNMENT:
            return "ASSIGNMENT";
        case TOKEN_TYPE_LEFT_BRACKET:
            return "LEFT_BRACKET";
        case TOKEN_TYPE_RIGHT_BRACKET:
            return "RIGHT_BRACKET";
        case TOKEN_TYPE_LEFT_QUOTE:
            return "LEFT_QUOTE";
        case TOKEN_TYPE_RIGHT_QUOTE:
            return "RIGHT_QUOTE";
        case TOKEN_TYPE_COMMA:
            return "COMMA";
        case TOKEN_TYPE_EOF:
            return "EOF";
    }
    return "UNKNOWN";
}

void print_tokens(void){
    for (int i = 0; i < tokens_count; ++i) {
        printf("Token: %s, %s\n", token_type_to_string(tokens[i].type), tokens[i].value);
    }
}

void free_token(Token* token){
    char* s = (token->value);
    free(s);
    //printf("FREEING %s\n", s);
}

void finish_parsing(void){
    for (int i = 0; i < tokens_count; ++i) {
        free_token(&tokens[i]);
    }
    free(tokens);
}

void clear_state(void){
    for (int i = 0; i < tokens_count; ++i) {
        free_token(&tokens[i]);
    }
    tokens_count = 0;
    is_in_token = false;
    current_continuous_token_type = TOKEN_TYPE_UNDEFINED;
    current_token_buffer_index = 0;
    last_line_index = 0;
}


// ### CONFIG PARSING

#define UNEXPECTED_EXCEPTION(expected) PUSH_EXCEPTION("Expected %s, got %s\n%s\n%s", expected,\
token_type_to_string(tokens[tk_flow].type),\
last_line,\
generate_underline_string(tokens[tk_flow].source_begin, tokens[tk_flow].source_end)\
);

int parse_variable_value__safe_realloc(ConfigVariable* var, size_t size){
    void* tmp = realloc(var->data.data, size);
    if(tmp == NULL){
        PUSH_EXCEPTION("Failed to reallocate array to size %ld", size);
        return 1;
    }
    var->data.data = tmp;
    return 0;
}

int parse_variable_value(int tk_flow, ConfigVariable* out_var){
    bool is_array = false;
    if(tokens[tk_flow].type == TOKEN_TYPE_LEFT_BRACKET){
        is_array = true;
        tk_flow++;
    }

    if(tokens_count <= tk_flow){
        UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_UNDEFINED))
        return 1;
    }

    enum TokenType var_type = TOKEN_TYPE_UNDEFINED;

    bool require_end_of_array = false;

    assert(out_var->data.data == NULL);

    while (1){
        // Array appending loop
        if(require_end_of_array && tokens[tk_flow].type != TOKEN_TYPE_RIGHT_BRACKET){
            UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_RIGHT_BRACKET))
            return 1;
        }

        if(tokens[tk_flow].type == TOKEN_TYPE_RIGHT_BRACKET){
            if(!is_array){
                UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_UNDEFINED))
                return 1;
            }
            else{
                // End of array
                break;
            }
        }

        out_var->count += 1;

        if(var_type != TOKEN_TYPE_UNDEFINED && tokens[tk_flow].type != var_type){
            UNEXPECTED_EXCEPTION(token_type_to_string(var_type))
            return 1;
        }
        var_type = tokens[tk_flow].type;

        int error = 0;
        int error2 = 0;

        if(tokens[tk_flow].type == TOKEN_TYPE_INTEGER){
            out_var->type = INTEGER;
            error2 = parse_variable_value__safe_realloc(out_var, out_var->count * sizeof(int64_t));
            if(error2 != 1)
                out_var->data.integer[out_var->count - 1] = parse_long(tokens[tk_flow].value, &error);
        }
        else if(tokens[tk_flow].type == TOKEN_TYPE_REAL){
            out_var->type = REAL;
            error2 = parse_variable_value__safe_realloc(out_var, out_var->count * sizeof(double));
            if(error2 != 1)
                out_var->data.real[out_var->count - 1] = parse_double(tokens[tk_flow].value, &error);
        }
        else if(tokens[tk_flow].type == TOKEN_TYPE_STRING){
            out_var->type = STRING;
            error2 = parse_variable_value__safe_realloc(out_var, out_var->count * sizeof(char*));
            if(error2 != 1)
                out_var->data.string[out_var->count - 1] = copy_string(tokens[tk_flow].value, &error);
        }
        else{
            UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_UNDEFINED));
            return 1;
        }

        if(error == 1 || error2 == 1){
            return 1;
        }

        if(!is_array){
            break; // Parsing single value
        }

        tk_flow++;
        if(tokens_count <= tk_flow){
            UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_RIGHT_BRACKET))
            return 1;
        }

        if(
                tokens[tk_flow].type != TOKEN_TYPE_COMMA &&
                tokens[tk_flow].type != TOKEN_TYPE_RIGHT_BRACKET
                ){
            UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_COMMA))
            return 1;
        }

        if(tokens[tk_flow].type == TOKEN_TYPE_COMMA){
            tk_flow++;
        }
        else{
            require_end_of_array = true;
        }

        if(tokens_count <= tk_flow){
            UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_UNDEFINED))
            return 1;
        }
    }

    tk_flow++;
    if(tokens_count > tk_flow){

        UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_EOF))
        return 1;
    }

    return 0;
}

int parse_current_tokens(void){
    if(tokens_count == 0){
        return 0;
    }

    int tk_flow = 0;

    if(tokens[tk_flow].type != TOKEN_TYPE_IDENTIFIER){
        UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_IDENTIFIER))
        return 1;
    }

    if(tokens_count < 2){
        PUSH_EXCEPTION("Unexpected end of line\n%s", last_line);
        return 1;
    }


    tk_flow++;

    if(tokens[tk_flow].type != TOKEN_TYPE_ASSIGNMENT){
        UNEXPECTED_EXCEPTION(token_type_to_string(TOKEN_TYPE_ASSIGNMENT))
        return 1;
    }

    if(tokens_count < 3){
        PUSH_EXCEPTION("Unexpected end of line\n%s", last_line);
        return 1;
    }

    tk_flow++;

    ConfigVariable var = {0};
    if(parse_variable_value(tk_flow, &var) == 1){
        return 1;
    }

    int error = 0;
    var.name = copy_string(tokens[0].value, &error);
    if(error == 1){
        return 1;
    }

    resize_parsed_vars_if_need();
    parsed_vars[parsed_vars_count++] = var;


    return 0;
}


int config_parser_parse__finish_line(void){
    if(config_parser_finish() == 1){
        return 1;
    }
    if(parse_current_tokens() == 1){
        return 1;
    }
    clear_state();
    return 0;
}

void config_parser_get_parsed_vars(ConfigVariable** out_vars, int* out_count){
    *out_vars = parsed_vars;
    *out_count = parsed_vars_count;

    parsed_vars = NULL;
    parsed_vars_count = 0;
}

int config_parser_parse_file(const char* path) {
    FILE* config = fopen(path, "r");
    if(config == NULL){
        if(errno == ENOENT) {
            LOG(LOG_ERROR, "File %s not found", path);
        }
        else if(errno == EACCES) {
            LOG(LOG_ERROR, "Have no permissions for file %s", path);
        }
        else{
            LOG(LOG_ERROR, "Error opening file %s: %s", path, strerror(errno));
        }

        PUSH_EXCEPTION("Failed to open config file: %s", path);
        return 1;
    }

    char* spaces = "                                                                 ";

    bool error = false;
    int at;

    bool is_in_comment = false;

    int c;
    while((c = fgetc(config)) != EOF){
        if(c == '#'){
            is_in_comment = true;
        }

        if(is_in_comment && c != '\n'){
            continue;
        }
        else if(c == '\n') {
            is_in_comment = false;
        }

        last_line[last_line_index++] = (char)c;
        if(last_line_index + 1 >= BUFFER_SIZE){ // +1 for '\0' in the end
            PUSH_EXCEPTION("Line buffer size reached (%d)", BUFFER_SIZE);
            fclose(config);
            return 1;
        }

        if(c == '\n'){
            // Line parsing for token finished; continue next stage
            if(config_parser_parse__finish_line() == 1){
                fclose(config);
                return 1;
            }
        }
        else if(config_parser_push_char((char)c) == 1){
            error = true;
            at = last_line_index;
            break;
        }
    }



    if(!error){
        if(config_parser_parse__finish_line() == 1){
            fclose(config);
            return 1;
        }
        fclose(config);
        return 0;
    }

    //read the rest of the line
    while ((c = fgetc(config)) && c != EOF && c != '\n'){
        last_line[last_line_index++] = (char)c;
        if(last_line_index + 1 >= BUFFER_SIZE){ // +1 for '\0' in the end
            PUSH_EXCEPTION("Line buffer size reached (%d)", BUFFER_SIZE);
            fclose(config);
            return 1;
        }
    }

    PUSH_EXCEPTION(
            "Error while parsing config\n%s\n%.*s^ Here",
            last_line,
            at - 1,
            spaces
    );

    fclose(config);
    return 1;
}

void config_parser_finish_parsing_file(void){
    if(parsed_vars != NULL){
        for (int i = 0; i < parsed_vars_count; ++i) {
            free_variable(&parsed_vars[i]);
        }
        free(parsed_vars);
        parsed_vars = NULL;
    }

    parsed_vars_count = 0;
}
