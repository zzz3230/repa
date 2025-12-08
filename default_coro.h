//
// Created by zzz32 on 12/6/2025.
//

#ifndef REPA_SERVER_DEFAULT_CORO_H
#define REPA_SERVER_DEFAULT_CORO_H

#include "coroutine_pool/coroutine_pool.h"

void init_default_scheduler();

coro_sched_t* get_default_scheduler();

#endif //REPA_SERVER_DEFAULT_CORO_H
