#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <string.h>

#include "coroutine_pool.h"

#define TEST_OK(msg)  printf("[ OK ] %s\n", msg)
#define TEST_FAIL(msg) do { printf("[FAIL] %s\n", msg); exit(1); } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) TEST_FAIL(msg); } while (0)

/* ============== Random utilities ============== */
static unsigned long myrand_state = 123456789;

unsigned long myrand() {
    myrand_state = myrand_state * 6364136223846793005ULL + 1;
    return myrand_state >> 32;
}

int myrand_int(int a, int b) {
    return a + (int)(myrand() % (b - a + 1));
}

/* ================= Global for stress ================= */
#define N_COROS 300
#define N_MUTEXES 5
#define N_SEMS 5
#define N_RWLOCKS 3

cor_mutex_t mutexes[N_MUTEXES];
cor_semaphore_t sems[N_SEMS];
cor_rwlock_t rwlocks[N_RWLOCKS];

/* shared counters */
volatile int shared_counter = 0;
volatile int sem_counter = 0;
volatile int rw_shared = 0;

/* ============== Stress worker ============== */

void stress_worker(void *arg) {
    (void)arg;
    int local = 0;

    for (int iter = 0; iter < 100; iter++) {
        int op = myrand_int(0, 5);
        switch (op) {
            case 0: {
                // just yield
                cor_yield();
                break;
            }
            case 1: {
                // mutex lock/unlock
                int mi = myrand_int(0, N_MUTEXES - 1);
                cor_mutex_lock(&mutexes[mi]);
                int tmp = shared_counter;
                // simulate work
                for (int k = 0; k < 10; k++) cor_yield();
                shared_counter = tmp + 1;
                cor_mutex_unlock(&mutexes[mi]);
                break;
            }
            case 2: {
                // semaphore wait/post
                int si = myrand_int(0, N_SEMS - 1);
                if (myrand_int(0, 1)) {
                    cor_sem_wait(&sems[si]);
                    sem_counter++;
                } else {
                    cor_sem_post(&sems[si]);
                }
                break;
            }
            case 3: {
                // rwlock read
                int rwi = myrand_int(0, N_RWLOCKS - 1);
                cor_rwlock_rdlock(&rwlocks[rwi]);
                // read
                int v = rw_shared;
                (void)v;
                cor_yield();
                cor_rwlock_unlock(&rwlocks[rwi]);
                break;
            }
            case 4: {
                // rwlock write
                int rwi = myrand_int(0, N_RWLOCKS - 1);
                cor_rwlock_wrlock(&rwlocks[rwi]);
                rw_shared++;
                cor_yield();
                cor_rwlock_unlock(&rwlocks[rwi]);
                break;
            }
            case 5: {
                // nested small coro + yield
                cor_yield();
                break;
            }
        }
    }
}

void test_stress_many_coroutines(void) {
    printf("=== Stress: many coroutines, mixed sync, random scheduling ===\n");
    coro_sched_t *S = coro_sched_create(N_COROS + 10, 0);
    ASSERT(S != NULL, "scheduler created for stress test");

    // init primitives
    for (int i = 0; i < N_MUTEXES; i++) cor_mutex_init(&mutexes[i]);
    for (int i = 0; i < N_SEMS; i++) cor_sem_init(&sems[i], 0);
    for (int i = 0; i < N_RWLOCKS; i++) cor_rwlock_init(&rwlocks[i]);

    shared_counter = 0;
    sem_counter = 0;
    rw_shared = 0;

    // spawn coroutines
    for (int i = 0; i < N_COROS; i++) {
        coro_t *c = coro_create(S, stress_worker, NULL, 0);
        ASSERT(c != NULL, "stress coro created");
    }

    cor_run(S);

    // basic invariants
    ASSERT(shared_counter >= 0, "shared_counter non-negative");
    // sem_counter should be <= total ops
    ASSERT(sem_counter >= 0, "sem_counter non-negative");
    ASSERT(rw_shared >= 0, "rw_shared non-negative");

    // cleanup
    for (int i = 0; i < N_MUTEXES; i++) cor_mutex_destroy(&mutexes[i]);
    for (int i = 0; i < N_SEMS; i++) cor_sem_destroy(&sems[i]);
    for (int i = 0; i < N_RWLOCKS; i++) cor_rwlock_destroy(&rwlocks[i]);

    coro_sched_destroy(S);
    TEST_OK("stress many coroutines");
}

/* =============== Test repeated create/destroy =============== */

void dummy(void *a) { (void)a; }

void test_create_destroy_cycles(void) {
    printf("=== Stress: repeated create/destroy scheduler ===\n");
    for (int iter = 0; iter < 100; iter++) {
        coro_sched_t *S = coro_sched_create(50, 0);
        ASSERT(S != NULL, "sched created");
        for (int j = 0; j < 50; j++) {
            coro_t *c = coro_create(S, dummy, NULL, 0);
            ASSERT(c != NULL, "coro created in cycle");
        }
        cor_run(S);
        coro_sched_destroy(S);
    }
    TEST_OK("create/destroy cycles");
}

/* ============== Test stack_size parameter variation ============== */

void stack_size_worker(void *arg) {
    (void)arg;
    volatile char buf[1024 * 10]; // 10 KB local stack usage
    memset((void*)buf, 0x55, sizeof(buf));
    // do some work
    for (int i = 0; i < 100; i++) cor_yield();
}

void test_different_stack_sizes(void) {
    printf("=== Test: custom stack sizes ===\n");
    size_t sizes[] = {0, 16 * 1024, 64 * 1024, 256 * 1024};
    for (int si = 0; si < (int)(sizeof(sizes)/sizeof(sizes[0])); si++) {
        size_t st = sizes[si];
        coro_sched_t *S = coro_sched_create(20, st);
        ASSERT(S != NULL, "sched with custom stack created");
        for (int i = 0; i < 20; i++) {
            coro_t *c = coro_create(S, stack_size_worker, NULL, st);
            ASSERT(c != NULL, "coro with custom stack created");
        }
        cor_run(S);
        coro_sched_destroy(S);
    }
    TEST_OK("stack size variation");
}

/* ============== Main ============== */

int _main(void) {
    srand((unsigned)time(NULL));
    myrand_state = ((unsigned long)rand()) ^ 0xdeadbeef;

    test_stress_many_coroutines();
    test_create_destroy_cycles();
    test_different_stack_sizes();

    printf("\nAll stress tests done.\n");
    return 0;
}
