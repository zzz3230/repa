//
// Created by zzz32 on 9/22/2025.
//

#include "resp_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define RESP_SIMPLE_STRING_PREFIX '+'
#define RESP_ERROR_PREFIX '-'
#define RESP_INTEGER_PREFIX ':'
#define RESP_BULK_STRING_PREFIX '$'
#define RESP_ARRAY_PREFIX '*'
#define RESP_NULL_STRING "$-1\r\n"
#define RESP_NULL_ARRAY "*-1\r\n"

bool is_end_of_string_is_rn(GString* str){
    return str->len >= 2 &&
            str->str[str->len - 2] == '\r' &&
            str->str[str->len - 1] == '\n';
}

bool specify_next_type(RespParser* parser, char symbol){
    switch (symbol) {
        case RESP_SIMPLE_STRING_PREFIX:
            parser->ctx.state = STATE_SIMPLE_STRING;
            return TRUE;
        case RESP_BULK_STRING_PREFIX:
            parser->ctx.state = STATE_BULK_STRING_LENGTH;
            return TRUE;
        case RESP_ARRAY_PREFIX:
            parser->ctx.state = STATE_ARRAY_LENGTH;
            return TRUE;
        case RESP_TYPE_INTEGER:
            parser->ctx.state = STATE_INTEGER;
            return TRUE;
        default:
            LOG(LOG_ERROR, "Unknown resp type '%c'", symbol);
            parser->ctx.state = STATE_UNKNOWN;
            return FALSE;
    }
}

void simple_string_received(RespParser* parser){
    GString* str = g_string_new(parser->buffer->str);
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_STRING;
    data->data.string_val = str;

    if(parser->ctx.is_array){
        g_array_append_val(parser->result->data.array_val, data);
        parser->ctx.array_elements_received++;
        //printf("to array %s\n", data->data.string_val->str);
    }
    else{
        parser->result = data;
        //printf("to self %s\n", data->data.string_val->str);
    }
    g_string_truncate(parser->buffer, 0);
}

void array_len_received(RespParser* parser){
    gchar *endptr = NULL;
    gint64 val = g_ascii_strtoll(parser->buffer->str, &endptr, 10);

    parser->ctx.array_elements_received = 0;
    parser->ctx.array_elements_expected = 0;

    if (*endptr != '\0') {
        LOG(LOG_ERROR, "Ой, в строке лишние символы: %s\n", endptr);
    } else {
        parser->ctx.array_elements_expected = val;
    }
    g_string_truncate(parser->buffer, 0);

    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_ARRAY;
    data->data.array_val = g_array_new(FALSE, TRUE, sizeof(RespData*));
    parser->result = data;
}

void bulk_string_len_received(RespParser* parser){
    gchar *endptr = NULL;
    gint64 val = g_ascii_strtoll(parser->buffer->str, &endptr, 10);

    if (*endptr != '\0') {
        LOG(LOG_ERROR, "Ой, в строке лишние символы: %s\n", endptr);
    } else {
        parser->ctx.bulk_string_length = val;
    }
    g_string_truncate(parser->buffer, 0);
}
void bulk_string_received(RespParser* parser){
    GString* str = g_string_new_len(parser->buffer->str, (uint)parser->buffer->len);
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_BULK_STRING;
    data->data.string_val = str;

    if(parser->ctx.is_array){
        g_array_append_val(parser->result->data.array_val, data);
        parser->ctx.array_elements_received++;
    }
    else{
        parser->result = data;
    }
    g_string_truncate(parser->buffer, 0);
}

void integer_received(RespParser* parser){
    gchar *endptr = NULL;
    gint64 val = g_ascii_strtoll(parser->buffer->str, &endptr, 10);

    if (*endptr != '\0') {
        LOG(LOG_ERROR, "Ой, в строке лишние символы: %s\n", endptr);
    } else {

        RespData* data = g_new0(RespData, 1);
        data->type = RESP_TYPE_BULK_STRING;
        data->data.integer_val = val;

        if(parser->ctx.is_array){
            g_array_append_val(parser->result->data.array_val, data);
            parser->ctx.array_elements_received++;
        }
        else{
            parser->result = data;
        }
    }

    g_string_truncate(parser->buffer, 0);
}

void reset_state(RespParser* parser){
    if(parser->ctx.state == STATE_END){
        parser->ctx.is_array = FALSE;
        g_string_truncate(parser->buffer, 0);
        if(parser->result != NULL){
            LOG(LOG_WARNING, "Parsing result leaked");
        }
        parser->result = NULL;
        parser->ctx.state = STATE_START;
        return;
    }

    if(parser->ctx.is_array){
        if(parser->ctx.array_elements_expected == parser->ctx.array_elements_received){
            parser->ctx.state = STATE_END;
        }
        else{
            parser->ctx.state = STATE_ARRAY_ELEMENTS;
        }
    }
    else{
        parser->ctx.state = STATE_END;
    }
}

bool receive_n_to_the_buffer_end_with_rn(RespParser* parser, const char *data, size_t len, size_t* processed, size_t n){
    int i = 0;
    while (TRUE){
        g_string_append_c(parser->buffer, data[i]);
        if(parser->buffer->len >= n + 2 /*"\r\n"*/ && !is_end_of_string_is_rn(parser->buffer)){
            LOG(LOG_ERROR, "End of bulk string not \\r\\n but '%c%c'",
                parser->buffer->str[parser->buffer->len - 2],
                parser->buffer->str[parser->buffer->len - 1]
                );
        }

        if(parser->buffer->len >= n + 2 && is_end_of_string_is_rn(parser->buffer))
        {
            g_string_truncate(parser->buffer, parser->buffer->len - 2);
            *processed = i + 1;
            return TRUE;
        }
        if(i >= len - 1){
            break;
        }
        i++;
    }
    *processed = i + 1;
    return FALSE;
}

bool receive_to_the_buffer_until_rn(RespParser* parser, const char *data, size_t len, size_t* processed){
    int i = 0;
    while (TRUE){
        g_string_append_c(parser->buffer, data[i]);
        if(is_end_of_string_is_rn(parser->buffer))
        {
            g_string_truncate(parser->buffer, parser->buffer->len - 2);
            *processed = i + 1;
            return TRUE;
        }
        if(i >= len - 1){
            break;
        }
        i++;
    }
    *processed = i + 1;
    return FALSE;
}

bool resp_parser_feed(RespParser *parser, const char *data, size_t len, size_t* processed) {
    int i;
    if(len == 0){
        parser->ctx.state = STATE_END;
    }

    switch (parser->ctx.state) {
        case STATE_UNKNOWN:
            if(receive_to_the_buffer_until_rn(parser, data, len, processed)){
                reset_state(parser);
            }
            break;

        case STATE_ARRAY_ELEMENTS:
        case STATE_START:
            if(!specify_next_type(parser, data[0])){
                *processed = -1;
                return TRUE;
            }
            *processed = 1;
            break;

        case STATE_SIMPLE_STRING:
            if(receive_to_the_buffer_until_rn(parser, data, len, processed)){
                simple_string_received(parser);
                reset_state(parser);
            }
            break;

        case STATE_ERROR:
            break;
        case STATE_INTEGER:
            if(receive_to_the_buffer_until_rn(parser, data, len, processed)){
                integer_received(parser);
                reset_state(parser);
            }
            break;
        case STATE_BULK_STRING_LENGTH:
            if(receive_to_the_buffer_until_rn(parser, data, len, processed)){
                bulk_string_len_received(parser);
                parser->ctx.state = STATE_BULK_STRING_DATA;
            }
            break;
        case STATE_BULK_STRING_DATA:
            if(receive_n_to_the_buffer_end_with_rn(parser, data, len, processed, parser->ctx.bulk_string_length)){
                bulk_string_received(parser);
                reset_state(parser);
            }
            break;
        case STATE_ARRAY_LENGTH:
            if(receive_to_the_buffer_until_rn(parser, data, len, processed)){
                array_len_received(parser);
                parser->ctx.is_array = true;
                parser->ctx.state = STATE_ARRAY_ELEMENTS;
            }
            break;
        case STATE_END:
            break;
    }
    return (parser->ctx.state == STATE_END);
}

GString* resp_data_to_string(const RespData *resp_data) {
    if (!resp_data) return g_string_new("NULL");
    GString *result = g_string_new(NULL);

    switch (resp_data->type) {
        case RESP_TYPE_STRING:
            g_string_printf(result, "String(%s)", resp_data->data.string_val->str);
            break;

        case RESP_TYPE_ERROR:
            g_string_printf(result, "Error(%s)", resp_data->data.string_val->str);
            break;

        case RESP_TYPE_INTEGER:
            g_string_printf(result, "Integer(%lld)", resp_data->data.integer_val);
            break;

        case RESP_TYPE_BULK_STRING:
            g_string_printf(result, "BulkString[%zu](%s)",
                            resp_data->data.string_val->len, resp_data->data.string_val->str);
            break;

        case RESP_TYPE_ARRAY:
            g_string_append(result, "Array[");
            for (guint i = 0; i < resp_data->data.array_val->len; i++) {
                RespData *element = g_array_index(resp_data->data.array_val, RespData*, i);
                GString *element_str = resp_data_to_string(element);
                g_string_append(result, element_str->str);
                if (i < resp_data->data.array_val->len - 1) {
                    g_string_append(result, ", ");
                }
                g_string_free(element_str, TRUE);
            }
            g_string_append(result, "]");
            break;

        case RESP_TYPE_NULL:
            g_string_append(result, "NULL");
            break;
    }

    return result;
}

RespData *resp_parser_get_result(RespParser *parser) {
    RespData* data = parser->result;
    parser->result = NULL;
    return data;
}

RespParser *resp_parser_new(void) {
    RespParser* parser = g_new0(RespParser, 1);
    parser->buffer = g_string_new("");
    parser->ctx.state = STATE_START;
    return parser;
}

void print_resp_data(const RespData *data) {
    if (!data) {
        LOG(LOG_ERROR, "NULL RespData");
        return;
    }

    switch (data->type) {
        case RESP_TYPE_STRING:
            LOG(LOG_INFO, "Simple string: %s", data->data.string_val->str);
            break;

        case RESP_TYPE_ERROR:
            LOG(LOG_INFO, "Error: %s", data->data.string_val->str);
            break;

        case RESP_TYPE_INTEGER:
            LOG(LOG_INFO, "Integer: %lld", (long long)data->data.integer_val);
            break;

        case RESP_TYPE_BULK_STRING:
            if (data->data.string_val)
                LOG(LOG_INFO, "Bulk string: \"%s\" (len=%d)",
                    data->data.string_val->str,
                    (int)data->data.string_val->len);
            else
                LOG(LOG_INFO, "Bulk string: NULL");
            break;

        case RESP_TYPE_ARRAY:
            LOG(LOG_INFO, "Array with %d elements", data->data.array_val->len);
            for (guint i = 0; i < data->data.array_val->len; i++) {
                RespData *elem = g_array_index(data->data.array_val, RespData*, i);
                print_resp_data(elem);
            }
            break;

        case RESP_TYPE_NULL:
            LOG(LOG_INFO, "Null");
            break;

        default:
            LOG(LOG_WARNING, "Unknown RESP type %d", data->type);
            break;
    }
}

void resp_parser_free(RespParser *parser){
    g_string_free(parser->buffer, TRUE);
    g_free(parser);
}

void resp_data_free(RespData *resp_data){ // NOLINT(*-no-recursion)
    if(resp_data->type == RESP_TYPE_ARRAY){
        for (int i = 0; i < resp_data->data.array_val->len; ++i) {
            resp_data_free(g_array_index(resp_data->data.array_val, RespData*, i));
        }
        g_array_free(resp_data->data.array_val, TRUE);
    }
    else if((resp_data->type & RESP_ANY_STRING_MASK) == RESP_TYPE_ANY_STRING){
        g_string_free(resp_data->data.string_val, TRUE);
    }
    else if(resp_data->type == RESP_TYPE_INTEGER){}
    else {
        LOG(LOG_ERROR, "Unknown resp data type %d", resp_data->type);
    }
    g_free(resp_data);
}