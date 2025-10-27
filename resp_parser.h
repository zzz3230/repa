//
// Created by zzz32 on 9/22/2025.
//

#ifndef REPA_SERVER_RESP_PARSER_H
#define REPA_SERVER_RESP_PARSER_H

#include <glib.h>
#include <stdint.h>
#include <stdbool.h>
#include "logger.h"

typedef enum {
    RESP_TYPE_INTEGER       = 0x02,
    RESP_TYPE_ARRAY         = 0x03,
    RESP_TYPE_ERROR         = 0x14,
    RESP_TYPE_BULK_STRING   = 0x24,
    RESP_TYPE_STRING        = 0x34,
    RESP_TYPE_NULL          = 0x05,

    RESP_TYPE_ANY_STRING    = 0x04,
    RESP_ANY_STRING_MASK    = 0x0F
} RespDataType;

typedef struct RespData RespData;

struct RespData {
    RespDataType type;
    union {
        GString *string_val;
        int64_t integer_val;
        GArray *array_val; // Array of RespData*
    } data;
};

typedef enum {
    STATE_START,
    STATE_END,
    STATE_SIMPLE_STRING,
    STATE_ERROR,
    STATE_INTEGER,
    STATE_BULK_STRING_LENGTH,
    STATE_BULK_STRING_DATA,
    STATE_ARRAY_LENGTH,
    STATE_ARRAY_ELEMENTS,
    STATE_UNKNOWN
} ParserState;

typedef struct {
    ParserState state;
    bool is_array;
    RespData *current_data;
    size_t bulk_string_length;
    size_t array_elements_expected;
    size_t array_elements_received;
    GArray *array; // Для массивов
} ParserContext;

typedef struct s_resp_parser {
    RespData *result;
    GString *buffer;
    GArray *stack; // Stack for nested arrays
    size_t expected_length;
    bool in_bulk_string;
    bool complete;
    GString *error;
    size_t bytes_processed; // Количество обработанных байт в текущем вызове
    ParserContext ctx;
} RespParser;

// Function prototypes
RespParser* resp_parser_new(void);
void resp_parser_free(RespParser *parser);
bool resp_parser_feed(RespParser *parser, const char *data, size_t len, size_t* processed);
RespData* resp_parser_get_result(RespParser *parser);
void reset_state(RespParser* parser);
bool resp_parser_is_complete(const RespParser *parser);
const char* resp_parser_get_error(const RespParser *parser);

// Новые функции для управления состоянием парсера
void resp_parser_reset(RespParser *parser);
size_t resp_parser_get_remaining_bytes(const RespParser *parser);
const char* resp_parser_get_remaining_data(const RespParser *parser);

// RespData functions
void resp_data_free(RespData *resp_data);
RespData* resp_data_copy(const RespData *src);
GString* resp_data_to_string(const RespData *resp_data);

void print_resp_data(const RespData *data);

#endif //REPA_SERVER_RESP_PARSER_H
