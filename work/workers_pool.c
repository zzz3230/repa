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
#include "../default_coro.h"

//pthread_t *pthreads;
bool *pthreads_finished;

GString **workers_buffers;

gint total_stop = 0;
cor_mutex_t queue_mutex;

GQueue *clients_queue;
int workers_count;

ClientExecutionCallback client_executed_callback;

void set_client_executed_client(ClientExecutionCallback cb) {
    client_executed_callback = cb;
}


void do_task(ClientTask *task, long worker_id) {
    //LOG(LOG_INFO, "Doing W(%ld) CL(%ld) ID(%ld)", worker_id, task->client->id, task->task_id);

    RespData *result = process_command(task->command, task->client);

    //LOG(LOG_INFO, "Done");

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

void do_client_tasks(RepaClient *client, long worker_id) {
    //

    while (!g_atomic_int_get(&total_stop)) {
        if (client->commands_queue->length <= 0) {
            break;
        }
        //cor_mutex_lock(&client->mutex);
        ClientTask *task = g_queue_pop_head(client->commands_queue);
        //cor_mutex_unlock(&client->mutex);

        do_task(task, worker_id);

        client_task_free(task);
    }
}

int queue_has_task() {
    return clients_queue->length > 0;
}

void worker_impl(void *worker_id_data) {

    long *worker_id = worker_id_data;


    while (!g_atomic_int_get(&total_stop)) {
        while (!g_atomic_int_get(&total_stop) && !queue_has_task()) {
            cor_wait_on(&queue_mutex);
        }

        if (g_atomic_int_get(&total_stop)) {
            break;
        }
        //cor_mutex_lock(&queue_mutex);
        //Getting client
        RepaClient *client = g_queue_pop_head(clients_queue);
        g_atomic_int_set(&client->is_doing_tasks, TRUE);
        g_atomic_int_set(&client->is_in_working_queue, FALSE);
        //cor_mutex_unlock(&queue_mutex);

        //Processing task
        do_client_tasks(client, *worker_id);

        g_atomic_int_set(&client->is_doing_tasks, FALSE);

        if (client_executed_callback)
            client_executed_callback(client);
    }

    g_free(worker_id_data);
}


#define DEFAULT_WORKERS_COUNT 4

void init_workers_pool() {

    workers_count = get_int_config_var_or_default("workers", DEFAULT_WORKERS_COUNT, true);

    LOG(LOG_INFO, "Starting %d worker(s)", workers_count);


    pthreads_finished = g_new0(bool, workers_count);
    workers_buffers = g_new0(GString*, workers_count);

    clients_queue = g_queue_new();

    cor_mutex_init(&queue_mutex);

    for (int i = 0; i < workers_count; ++i) {
        long *worker_id = g_new0(long, 1);
        *worker_id = i;
        if(coro_create(get_default_scheduler(), worker_impl, worker_id, 0) == NULL){
            LOG(LOG_ERROR, "Unable to create worker coro, using only %d workers", i);
            workers_count = i;
            break;
        }
//        if(pthread_create(&pthreads[i], NULL, worker_impl, worker_id) != 0){
//            LOG(LOG_ERROR, "Unable to create worker thread, using only %d workers", i);
//            workers_count = i;
//            g_free(worker_id);
//            break;
//        }

        workers_buffers[i] = g_string_sized_new(128);
    }
}

void stop_all_workers() {
    g_atomic_int_set(&total_stop, TRUE);
    cor_notify_all(&queue_mutex);
}

void stop_workers_pool() {
    LOG(LOG_INFO, "Stopping workers pool");

    stop_all_workers();

    g_free(pthreads_finished);
    g_free(workers_buffers);
}

void add_client_to_workers_pool_if_require(RepaClient *client) {
    //cor_mutex_lock(&client->mutex);
    if (
            g_atomic_int_get(&client->is_in_working_queue) == TRUE ||
            g_atomic_int_get(&client->is_doing_tasks) == TRUE ||
            g_atomic_int_get(&client->marked_for_free) == TRUE) {
        return;
    }

    g_queue_push_head(clients_queue, client);
    g_atomic_int_set(&client->is_in_working_queue, TRUE);

    cor_notify_all(&queue_mutex);

}


