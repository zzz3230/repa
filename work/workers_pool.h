//
// Created by zzz32 on 9/21/2025.
//

#ifndef REPA_SERVER_WORKERS_POOL_H
#define REPA_SERVER_WORKERS_POOL_H

#include "../network/router.h"
typedef void (*ClientExecutionCallback)(RepaClient* client);

#define LOGWP_(level, format, ...)
#define LOGWP(level, format, ...) LOG(level, format)
extern pthread_cond_t client_finish_cond;
extern pthread_mutex_t client_doing_mutex;

void add_client_to_workers_pool_if_require(RepaClient* client);
void set_client_executed_client(ClientExecutionCallback cb);
void init_workers_pool();
void stop_workers_pool();
#endif //REPA_SERVER_WORKERS_POOL_H
