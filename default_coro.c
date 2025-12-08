//
// Created by zzz32 on 12/6/2025.
//

#include "default_coro.h"

coro_sched_t *default_scheduler = NULL;

void init_default_scheduler(){
    default_scheduler = coro_sched_create(100, 0);
}

coro_sched_t* get_default_scheduler(){
    return default_scheduler;
}