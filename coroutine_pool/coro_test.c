//
// Created by zzz32 on 11/9/2025.
//


#include <stdio.h>
#include <sys/poll.h>
#include "coroutine_pool.h"

cor_mutex_t mutex;

void coro1(void *arg0) {
    printf("Coro1\n");
    cor_mutex_lock(&mutex);
    {
        printf("1 Locked task begin\n");
        for (int i = 0; i < 10; ++i) {
            cor_sleep_ms(100);
            cor_yield();
        }
        printf("1 Locked task end\n");
    }
    cor_mutex_unlock(&mutex);

    printf("1 Waiting\n");
    cor_wait_on(&mutex);
    printf("1 Notify received\n");

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

    printf("2 Sending notify\n");
    cor_notify_all(&mutex);

    for (int i = 0; i < 20; ++i){
        printf("[%d]\n", i);
        cor_sleep_ms(1000);
    }

    printf("Coro2 finished\n");
}

void coro3(void *arg0){

    cor_wait_on(&mutex);

    while (true){
        poll_fd stdin_fd = {
                .fd = 0,
                .events = POLLIN
        };
        poll_fd_array polls = {
                .len = 1,
                .data = &stdin_fd
        };

        cor_poll(polls);

        if(!(polls.data[0].revents & POLLIN)){
            printf("No data\n");
        }
        else{
            char buffer[256];
            int count = (int)read(0, buffer, 256);
            buffer[count] = '\0';
            printf("Read %d bytes\n", count);
            printf("'%s'\n", buffer);
        }

        if(abs(0)) break;
    }
}



int __main() {
    printf("Init\n");

    coro_sched_t *sched = coro_sched_create(100, 0);

    cor_mutex_init(&mutex);

    coro_create(sched, coro1, NULL, 0);
    coro_create(sched, coro2, NULL, 0);
    coro_create(sched, coro3, NULL, 0);

    cor_run(sched);

    return 0;
}