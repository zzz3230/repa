//
// Created by zzz32 on 9/21/2025.
//

#include <pthread.h>
#include <stdio.h>
#include "router.h"
#include "../repa_client.h"
#include "../work/workers_pool.h"
#include "../resp_parser.h"

void init_router(){
}

void client_task_free(ClientTask* task){
    resp_data_free(task->command);
    g_free(task);
}

void push_command_task(RepaClient* client, RespData* command){
    //printf("PUSH BEGIN\n");
    ClientTask* task = g_new0(ClientTask, 1);
    task->client = client;
    task->command = command;
    task->task_id = client->last_command_task_id++;

    //cor_mutex_lock(&client->mutex);
    g_queue_push_head(client->commands_queue, task);


    //cor_mutex_unlock(&client->mutex);


    add_client_to_workers_pool_if_require(client);


}