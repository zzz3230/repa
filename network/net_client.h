//
// Created by zzz32 on 9/28/2025.
//

#ifndef REPA_SERVER_NET_CLIENT_H
#define REPA_SERVER_NET_CLIENT_H


#include "../resp_parser.h"

typedef struct s_net_client {
    int fd;
    RespParser* parser;
} NetClient;


void send_response_to_client(NetClient* client, char* data, size_t len);

#endif //REPA_SERVER_NET_CLIENT_H
