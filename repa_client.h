//
// Created by zzz32 on 9/21/2025.
//

#ifndef REPA_SERVER_REPA_CLIENT_H
#define REPA_SERVER_REPA_CLIENT_H

#include <glib.h>
#include "network/master_listener.h"
#include "network/net_client.h"
#include "default_coro.h"

typedef struct s_repa_client {
    NetClient* net_client;
    GQueue* commands_queue;
    cor_mutex_t mutex;
    long id;
    gint is_in_working_queue;
    gint is_doing_tasks;

    long last_command_task_id;
    bool authenticated;
    gint marked_for_free;
} RepaClient;

#endif //REPA_SERVER_REPA_CLIENT_H
