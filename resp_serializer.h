//
// Created by zzz32 on 9/29/2025.
//

#ifndef REPA_SERVER_RESP_SERIALIZER_H
#define REPA_SERVER_RESP_SERIALIZER_H

#include "resp_parser.h"

bool resp_data_serialize(GString* buffer, RespData* data);
RespData* resp_data_new_array(int len, ...);
RespData* resp_data_from_string(GString* str);
RespData* resp_data_from_error(GString* error_message);
RespData* resp_data_from_simple_string(GString* str);
RespData* resp_data_new_array_sized(int len);

#endif //REPA_SERVER_RESP_SERIALIZER_H
