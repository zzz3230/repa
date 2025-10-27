//
// Created by zzz32 on 10/18/2025.
//
#include <pthread.h>
#include <bits/pthreadtypes.h>
#include <unistd.h>
#include "db_services.h"
#include "../logger.h"
#include "db_backend.h"
void* services_impl(void* data);

pthread_t services_thread;
gint is_stop = false;

void start_db_services(){
    pthread_create(&services_thread, NULL, services_impl, NULL);
}
void stop_db_services(){
    LOG(LOG_INFO, "Begin stopping services");
    g_atomic_int_set(&is_stop, true);
    pthread_join(services_thread, NULL);
}
void join_db_services(){
    pthread_join(services_thread, NULL);
}

void do_join_service();
void* services_impl(void* data){
    while (!g_atomic_int_get(&is_stop)){
        do_join_service();
        for (int i = 0; i < 10; ++i) {
            if(g_atomic_int_get(&is_stop)) break;
            g_usleep(100000);
        }
    }
    LOG(LOG_INFO, "End stopping services");
    return NULL;
}

void do_join_service(){
    db_service_join_free_segments(get_master_db());
}