//
// Created by zzz32 on 9/21/2025.
//

#include <pthread.h>
#include <glib.h>
#include <stdio.h>
#include "workers_pool.h"
#include "../logger.h"
#include "../network/net_client.h"
#include "../db/command_processor.h"
#include "../resp_serializer.h"
#include "../db/stats_service.h"
#include "../util/config_extend.h"

pthread_t* pthreads;
GString** workers_buffers;

gint total_stop = 0;
pthread_cond_t workers_cond;
pthread_mutex_t queue_mutex;

GQueue* clients_queue;
int workers_count;

ClientExecutionCallback client_executed_callback;
void set_client_executed_client(ClientExecutionCallback cb){
    client_executed_callback = cb;
}


void do_task(ClientTask* task, long worker_id){
    //LOG(LOG_INFO, "Doing W(%ld) CL(%ld) ID(%ld)", worker_id, task->client->id, task->task_id);

    RespData* result = process_command(task->command, task->client);

    g_string_truncate(workers_buffers[worker_id], 0);
    resp_data_serialize(workers_buffers[worker_id], result);
    send_response_to_client(
            task->client->net_client,
            workers_buffers[worker_id]->str,
            workers_buffers[worker_id]->len
            );

    //LOGWP(LOG_INFO, "Done  W(%lld) CL(%d) ID(%d)", worker_id, task->client->id, task->task_id);

    resp_data_free(result);

}

void do_client_tasks(RepaClient* client, long worker_id){
    //

    while (!g_atomic_int_get(&total_stop)){
        pthread_mutex_lock(&client->mutex);
        if(client->commands_queue->length <= 0){
            pthread_mutex_unlock(&client->mutex);
            break;
        }
        ClientTask* task = g_queue_pop_head(client->commands_queue);
        pthread_mutex_unlock(&client->mutex);

        do_task(task, worker_id);

        client_task_free(task);
    }
}

int queue_has_task(){
    return clients_queue->length > 0;
}

void* worker_impl(void* worker_id_data){
    long* worker_id = worker_id_data;

    while (!g_atomic_int_get(&total_stop)){
        pthread_mutex_lock(&queue_mutex);
        while (!g_atomic_int_get(&total_stop) && !queue_has_task()){
            pthread_cond_wait(&workers_cond, &queue_mutex);
        }
        if(g_atomic_int_get(&total_stop)){
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        //Getting client
        RepaClient* client = g_queue_pop_head(clients_queue);
        g_atomic_int_set(&client->is_doing_tasks, TRUE);
        g_atomic_int_set(&client->is_in_working_queue, FALSE);
        pthread_mutex_unlock(&queue_mutex);

        //Processing task
        do_client_tasks(client, *worker_id);

        g_atomic_int_set(&client->is_doing_tasks, FALSE);

        if(client_executed_callback)
            client_executed_callback(client);
    }

    return NULL;
}



#define DEFAULT_WORKERS_COUNT 4
void init_workers_pool(){

    workers_count = get_int_config_var_or_default("workers", DEFAULT_WORKERS_COUNT, true);

    LOG(LOG_INFO, "Starting %d worker(s)", workers_count);


    pthreads = g_new0(pthread_t, workers_count);
    workers_buffers = g_new0(GString*, workers_count);

    clients_queue = g_queue_new();

    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&workers_cond, NULL);

    for (int i = 0; i < workers_count; ++i) {
        long* worker_id = g_new0(long, 1);
        *worker_id = i;
        pthread_create(&pthreads[i], NULL, worker_impl, worker_id);

        workers_buffers[i] = g_string_sized_new(128);
    }
}

void stop_all_workers(){
    g_atomic_int_set(&total_stop, TRUE);
    pthread_cond_broadcast(&workers_cond);
}

void stop_workers_pool(){
    LOG(LOG_INFO, "Stopping workers pool");
    stop_all_workers();

    for (int i = 0; i < workers_count; ++i) {
        pthread_join(pthreads[i], NULL);
    }
    for (int i = 0; i < workers_count; ++i) {
        g_string_free(workers_buffers[i], TRUE);
    }
}

void add_client_to_workers_pool_if_require(RepaClient* client){
    pthread_mutex_lock(&client->mutex);
    if(
        g_atomic_int_get(&client->is_in_working_queue) == TRUE ||
        g_atomic_int_get(&client->is_doing_tasks) == TRUE ||
        g_atomic_int_get(&client->marked_for_free) == TRUE){
        pthread_mutex_unlock(&client->mutex);
        return;
    }

    pthread_mutex_lock(&queue_mutex);
    g_queue_push_head(clients_queue, client);
    g_atomic_int_set(&client->is_in_working_queue, TRUE);
    pthread_cond_broadcast(&workers_cond);
    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_unlock(&client->mutex);
}


