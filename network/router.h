//
// Created by zzz32 on 9/21/2025.
//

#ifndef REPA_SERVER_ROUTER_H
#define REPA_SERVER_ROUTER_H

#include "../repa_client.h"
#include "../resp_parser.h"

typedef struct s_client_task {
    RepaClient* client;
    RespData* command;
    long task_id;
} ClientTask;

void push_command_task(RepaClient* client, RespData* command);
void client_task_free(ClientTask* task);
#endif //REPA_SERVER_ROUTER_H
