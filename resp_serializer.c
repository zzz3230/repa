//
// Created by zzz32 on 9/29/2025.
//

#include "resp_serializer.h"

bool resp_data_serialize(GString* buffer, RespData* data){ // NOLINT(*-no-recursion)T

    if(data->type == RESP_TYPE_ARRAY){
        g_string_append_printf(buffer, "*%u\r\n", data->data.array_val->len);
        for (int i = 0; i < data->data.array_val->len; ++i) {
            resp_data_serialize(buffer, g_array_index(data->data.array_val, RespData*, i));
        }
    }

    if(data->type == RESP_TYPE_BULK_STRING){
        g_string_append_printf(buffer, "$%u\r\n", data->data.string_val->len);
        g_string_append_len(buffer, data->data.string_val->str, data->data.string_val->len);
        g_string_append(buffer, "\r\n");
        return TRUE;
    }
    if(data->type == RESP_TYPE_STRING){
        g_string_append_c(buffer, '+');
        g_string_append(buffer, data->data.string_val->str);
        g_string_append(buffer, "\r\n");
        return TRUE;
    }
    if(data->type == RESP_TYPE_ERROR){
        g_string_append_c(buffer, '-');
        g_string_append(buffer, data->data.string_val->str);
        g_string_append(buffer, "\r\n");
        return TRUE;
    }
    return FALSE;
}

RespData* resp_data_new_array_sized(int len){
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_ARRAY;
    data->data.array_val = g_array_new(FALSE, TRUE, sizeof(RespData*));
    return data;
}

RespData* resp_data_new_array(int len, ...){
    va_list args;
    va_start(args, count);

    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_ARRAY;
    data->data.array_val = g_array_new(FALSE, TRUE, len);

    for (int i = 0; i < len; i++) {
        RespData* value = va_arg(args, RespData*);
        g_array_append_val(data->data.array_val, value);
    }

    va_end(args);

    return data;
}

RespData* resp_data_from_simple_string(GString* str){
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_STRING;
    data->data.string_val = str;
    return data;
}

RespData* resp_data_from_string(GString* str){
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_BULK_STRING;
    data->data.string_val = str;
    return data;
}
RespData* resp_data_from_error(GString* error_message){
    RespData* data = g_new0(RespData, 1);
    data->type = RESP_TYPE_ERROR;
    data->data.string_val = error_message;
    return data;
}