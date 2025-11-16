//
// Created by zzz32 on 11/9/2025.
//


#include <stdio.h>
#include "coroutine_pool.h"

cor_mutex_t mutex;

void coro1(void *arg0) {
    printf("Coro1\n");
    cor_mutex_lock(&mutex);
    {
        printf("1 Locked task begin\n");
        cor_sleep_ms(1000);
        printf("1 Locked task end\n");
    }
    cor_mutex_unlock(&mutex);
    printf("Coro1 finished\n");
}

void coro2(void *arg0) {
    printf("Coro2\n");
    cor_sleep_ms(100);
    cor_mutex_lock(&mutex);
    {
        printf("2 Locked task begin\n");
        cor_sleep_ms(1000);
        printf("2 Locked task end\n");
    }
    cor_mutex_unlock(&mutex);
    printf("Coro2 finished\n");
}

int _main() {
    printf("Init\n");

    coro_sched_t *sched = coro_sched_create(100, 0);

    cor_mutex_init(&mutex);

    coro_create(sched, coro1, NULL, 0);
    coro_create(sched, coro2, NULL, 0);

    cor_run(sched);

    return 0;
}