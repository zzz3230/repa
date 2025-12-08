
#include <ucontext.h>
#include <stdio.h>
#include <sys/poll.h>
#include <assert.h>
#include "coroutine_pool.h"


coro_t *current_coro = NULL;
coro_t *prev_coro = NULL;
coro_t *scheduler_coro = NULL;

void log_error(char *fmt) {
    fprintf(stderr, fmt);
}

void my_g_array_remove(GArray *arr, gpointer val) {
    for (int i = 0; i < arr->len; ++i) {
        if (g_array_index(arr, gpointer, i) == val) {
            g_array_remove_index_fast(arr, i);
            return;
        }
    }
}

void detach_coro_from_scheduler(coro_sched_t *sched, coro_t *c) {
    // ready queue
    GList *link = g_queue_find(sched->ready_coros, c);
    if (link) g_queue_delete_link(sched->ready_coros, link);

    // waiting sleeps
    my_g_array_remove(sched->waiting_sleeps_coros, c);

    // waiting fds + remove fd -> coro маппинги
    for (int i = 0; i < c->waiting_fds.len; ++i) {
        int fd = c->waiting_fds.data[i].fd;
        g_hash_table_remove(sched->fd_to_coro, GINT_TO_POINTER(fd));
    }
    my_g_array_remove(sched->waiting_fds_coros, c);

    // наконец — из all_coros (убираем ссылку на объект)
    my_g_array_remove(sched->all_coros, c);
}

long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void coro_stopwatch_start() {
    current_coro->stopwatch_elapsed_prev = 0;
    current_coro->stopwatch_begin_cur = now_ms();
}

long coro_stopwatch_elapsed() {
    return current_coro->stopwatch_elapsed_prev + now_ms() - current_coro->stopwatch_begin_cur;
}

void free_coro(coro_t *coro) {
    if (coro->freed) {
        log_error("Freed coro\n");
        exit(0);
    }
    coro->freed = true;
    free(coro->stack);
    g_free(coro);
}

void cleanup_all_coros(coro_sched_t *sched) {
    for (int i = 0; i < sched->all_coros->len; i++) {
        coro_t *coro = g_array_index(sched->all_coros, coro_t*, i);
        free_coro(coro);
    }
    g_array_set_size(sched->all_coros, 0);
}


void swap_coro(coro_t *to) {
    //printf("Swap to %p\n", to);
    if (to->finished) {
        log_error("Swap to finished\n");
    }
    prev_coro = current_coro;
    current_coro = to;

    if (prev_coro == NULL) {
        setcontext(&to->ctx);
        return;
    }

    to->status = RUNNING;
    prev_coro->stopwatch_elapsed_prev += now_ms() - prev_coro->stopwatch_begin_cur;
    to->stopwatch_begin_cur = now_ms();
    swapcontext(&prev_coro->ctx, &to->ctx);
}

void make_ready(coro_t *coro) {
    if (coro->status != READY) {
        coro->status = READY;
        g_queue_push_tail(coro->scheduler->ready_coros, coro);
    }
}

typedef struct {
    coro_t *coro;
    int fd_id;
} coro_fd_event_pair;

bool check_fds(coro_sched_t *sched, int timeout) {
    g_array_set_size(sched->_poll_fds, 0);
    for (int i = 0; i < sched->waiting_fds_coros->len; ++i) {
        coro_t *coro = g_array_index(sched->waiting_fds_coros, coro_t*, i);
        assert(coro->status == WAITING_FDS);
        for (int j = 0; j < coro->waiting_fds.len; ++j) {
            g_array_append_val(sched->_poll_fds, coro->waiting_fds.data[j]);
        }
    }

    if (sched->_poll_fds->len > 0) {
        poll((struct pollfd *) sched->_poll_fds->data, sched->_poll_fds->len, timeout);

        for (int i = 0; i < sched->_poll_fds->len; ++i) {
            struct pollfd pfd = g_array_index(sched->_poll_fds, struct pollfd, i);
            coro_fd_event_pair *waiting = g_hash_table_lookup(sched->fd_to_coro, GINT_TO_POINTER(pfd.fd));
            if (pfd.revents & waiting->coro->waiting_fds.data[waiting->fd_id].events) {
                waiting->coro->waiting_fds.data[waiting->fd_id].revents = pfd.revents;
                make_ready(waiting->coro);
                my_g_array_remove(sched->waiting_fds_coros, waiting->coro);

            }
        }
        return true;
    } else if (timeout > 0) {
        g_usleep(timeout * 1000);
    }
    return false;
}

bool check_sleeps(coro_sched_t *sched) {
    long now = now_ms();
    for (int i = 0; i < sched->waiting_sleeps_coros->len; ++i) {
        coro_t *coro = g_array_index(sched->waiting_sleeps_coros, coro_t*, i);
        assert(coro->status == WAITING_SLEEP);
        if (coro->awake_time <= now) {
            make_ready(coro);
            //printf("Ready&rem %d\n", coro->id);
            g_array_remove_index_fast(sched->waiting_sleeps_coros, i);
            i--; // так как текущий заменяется на последний
        }
    }
    return sched->waiting_sleeps_coros->len > 0;
}

void scheduler_coro_loop(void *arg0) {
    while (1) {
        coro_sched_t *scheduler = current_coro->scheduler;
        if (scheduler->finishing) {
            scheduler->finishing = false;
            scheduler->finished = true;
            cleanup_all_coros(scheduler);

            assert(scheduler->all_coros->len == 0);

            prev_coro = NULL;
            current_coro = NULL;
            swapcontext(&scheduler->coro->ctx, &scheduler->main_context);
            continue;
        }

        if (prev_coro != NULL && prev_coro != scheduler->coro) { //from other coroutine
            if (prev_coro->status == READY) {
                g_queue_push_tail(scheduler->ready_coros, prev_coro);
            }
            if (prev_coro->status == FINISHED) {
                assert(g_queue_find(scheduler->ready_coros, prev_coro) == NULL);
                detach_coro_from_scheduler(scheduler, prev_coro);
                prev_coro->finished = true;
                free_coro(prev_coro);
            }
            prev_coro = NULL;
        }

        int poll_timeout = scheduler->ready_coros->length > 0 ? 0 : 10;

        check_fds(scheduler, poll_timeout);
        check_sleeps(scheduler);

//        printf("FD[%d] SLEEP[%d] READY[%d] TOTAL[%d]\n",
//               (int) scheduler->waiting_fds_coros->len,
//               (int) scheduler->waiting_sleeps_coros->len,
//               (int) scheduler->ready_coros->length,
//               (int) cor_count_total(scheduler)
//        );

        //printf("%d\n", scheduler->ready_coros->length);
        if (scheduler->ready_coros->length > 0) {
            coro_t *next_coro = g_queue_pop_head(scheduler->ready_coros);
            assert(next_coro->status == READY);
            swap_coro(next_coro);
        } else if (cor_count_total(scheduler) == 0) {
            scheduler->finished = true;

            assert(scheduler->all_coros->len == 0);

            prev_coro = NULL;
            current_coro = NULL;
            swapcontext(&scheduler->coro->ctx, &scheduler->main_context);
            continue;
        }


    }


}


void free_cor_wait_token_info(gpointer data) {
    cor_wait_token_info *info = data;
    g_queue_free(info->waiters);
    g_free(info);
}

coro_sched_t *coro_sched_create(size_t max_coroutines, size_t stack_size) {
    coro_sched_t *sched = g_new0(coro_sched_t, 1);
    coro_sched_init(sched, max_coroutines, stack_size);
    return sched;
}

int coro_sched_init(coro_sched_t *sched, size_t max_coroutines, size_t stack_size) {
    sched->ready_coros = g_queue_new();
    sched->_poll_fds = g_array_new(FALSE, TRUE, sizeof(struct pollfd));
    sched->waiting_sleeps_coros = g_array_new(FALSE, TRUE, sizeof(coro_t *));
    sched->waiting_fds_coros = g_array_new(FALSE, TRUE, sizeof(coro_t *));
    sched->fd_to_coro = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    sched->waiting_tokens = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, free_cor_wait_token_info);
    sched->all_coros = g_array_new(FALSE, TRUE, sizeof(coro_t *));
    sched->coro = coro_create(sched, scheduler_coro_loop, NULL, stack_size);
    g_array_remove_index(sched->all_coros, 0);
    g_queue_pop_tail(sched->ready_coros);
    return 0;
}

void cor_run(coro_sched_t *sched) {
    sched->finished = false;
    getcontext(&sched->main_context);
    if (!sched->finished) {
        swap_coro(sched->coro);
    }
}

static void coro_entry_point(coro_fn_t func, void *arg) {
    coro_t *self = current_coro;
    func(arg);
    self->status = FINISHED;
    cor_yield();
}

int coro_ids = 0;

coro_t *coro_create(coro_sched_t *sched, coro_fn_t func, void *arg, size_t stack_size) {
    if (sched == NULL || func == NULL) {
        return NULL;
    }

    coro_t *coro = g_new0(coro_t, 1);

    coro->id = coro_ids++;
    coro->scheduler = sched;
    coro->status = READY;
    coro->awake_time = 0;


    if (stack_size == 0)
        stack_size = 64 * 1024;

    coro->stack = malloc(stack_size);
    assert(coro->stack && "failed to allocate stack");

    // инициализируем контекст
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = stack_size;
    if (sched->coro != NULL) {
        coro->ctx.uc_link = &sched->coro->ctx;
    }


    makecontext(&coro->ctx, (void (*)()) coro_entry_point, 2, func, arg);

    g_queue_push_tail(sched->ready_coros, coro);
    g_array_append_val(sched->all_coros, coro);

    return coro;
}

void coro_sched_destroy(coro_sched_t *sched) {
    g_queue_free(sched->ready_coros);
    g_array_free(sched->_poll_fds, TRUE);
    g_array_free(sched->waiting_fds_coros, TRUE);
    g_hash_table_destroy(sched->fd_to_coro);
    g_hash_table_destroy(sched->waiting_tokens);
    g_array_free(sched->waiting_sleeps_coros, TRUE);
    g_array_free(sched->all_coros, TRUE);
    g_free(sched);
}

void coro_sched_shutdown(coro_sched_t *sched) {
    sched->finishing = true;
}

coro_t *coro_current(void) {
    return current_coro;
}

uint64_t coro_id(coro_t *c) {
    return (uint64_t) c;
}

void cor_exit(void) {
    current_coro->status = FINISHED;
    cor_yield();
}

ssize_t cor_count_status(coro_sched_t *sched, CoroStatus status, const char *op) {

    static short iop_eq = 0;
    if (iop_eq == 0) iop_eq = *(short *) "==";

    static short iop_neq = 0;
    if (iop_neq == 0) iop_neq = *(short *) "!=";

    static short iop_and = 0;
    if (iop_and == 0) iop_and = *(short *) "&";

    short iop = *(short *) op;
    int count = 0;
    for (int i = 0; i < sched->all_coros->len; ++i) {
        bool result = 0;
        CoroStatus coroStatus = g_array_index(sched->all_coros, coro_t*, i)->status;
        if (iop == iop_eq) {
            result = coroStatus == status;
        } else if (iop == iop_neq) {
            result = coroStatus != status;
        } else if (iop == iop_and) {
            result = coroStatus & status;
        } else {
            log_error("Unknown op");
        }

        if (result) {
            count++;
        }
    }
    return count;
}

size_t cor_count_total(coro_sched_t *sched) {
    return cor_count_status(sched, FINISHED, "!=");
}

size_t cor_count_ready(coro_sched_t *sched) {
    return cor_count_status(sched, READY, "==");
}

size_t cor_count_waiting(coro_sched_t *sched) {
    return cor_count_status(sched, WAITING_ANY, "&");
}

const char *coro_state_name(coro_t *c) {
    if (c->status & WAITING_ANY)
        return "WAITING";
    if (c->status == READY)
        return "READY";
    if (c->status == FINISHED)
        return "FINISHED";
    if (c->status == RUNNING)
        return "RUNNING";
    return "UNKNOWN_STATE";
}

int cor_sleep_ms(unsigned int ms) {
    current_coro->awake_time = now_ms() + ms;
    g_array_append_val(current_coro->scheduler->waiting_sleeps_coros, current_coro);
    current_coro->status = WAITING_SLEEP;
    cor_yield();
    return 1;
}


void cor_yield(void) {
    //printf("Coro %p\n", current_coro);
    if (current_coro->status == RUNNING) {
        current_coro->status = READY;
    }
    swap_coro(current_coro->scheduler->coro);
}

int cor_poll(poll_fd_array fds) {
    if (fds.len == 0) {
        return 0;
    }

    current_coro->waiting_fds = fds;
    g_array_append_val(current_coro->scheduler->waiting_fds_coros, current_coro);
    for (int i = 0; i < fds.len; ++i) {
        coro_fd_event_pair *pair = g_new0(coro_fd_event_pair, 1);
        pair->coro = current_coro;
        pair->fd_id = i;
        g_hash_table_insert(current_coro->scheduler->fd_to_coro, GINT_TO_POINTER(fds.data[i].fd), pair);
        //printf("Ins %d\n", fds.data[i].fd);
    }
    current_coro->status = WAITING_FDS;

    cor_yield();

    for (int i = 0; i < fds.len; ++i) {
        g_hash_table_remove(current_coro->scheduler->fd_to_coro, GINT_TO_POINTER(fds.data[i].fd));
    }
    return 1;
}

void cor_mutex_init(cor_mutex_t *m) {
    m->locked = 0;
    m->owner = NULL;
    m->waiters = g_queue_new();
}

void cor_mutex_lock(cor_mutex_t *m) {
    if (!m->locked) {
        m->locked = 1;
        m->owner = current_coro;
    } else {
        g_queue_push_tail(m->waiters, current_coro);
        current_coro->status = WAITING_MUTEX;
        cor_yield();
    }
}

int cor_mutex_trylock(cor_mutex_t *m) {
    if (!m->locked) {
        m->locked = 1;
        m->owner = current_coro;
        return 0;
    }
    return 1;
}

void cor_mutex_destroy(cor_mutex_t *m) {
    if (m->locked) {
        log_error("Destroying locked mutex");
    }
    g_queue_free(m->waiters);
}

void cor_mutex_unlock(cor_mutex_t *m) {
    if (!m->locked) {
        return;
    }
    if (m->owner != current_coro) {
        log_error("Bad coro unlocking mutex");
        return;
    }
    m->locked = 0;
    m->owner = NULL;
    if (m->waiters->length > 0) {
        m->locked = 1;
        make_ready(m->owner = g_queue_pop_head(m->waiters));
    }
    //cor_yield();
}


void cor_sem_init(cor_semaphore_t *s, int initial) {
    s->value = initial;
    s->waiters = g_queue_new();
}

void cor_sem_wait(cor_semaphore_t *s) {
    if (s->value > 0) {
        s->value--;
    } else {
        g_queue_push_tail(s->waiters, current_coro);
        current_coro->status = WAITING_SEMAPHORE;
        cor_yield();
    }
}

int cor_sem_trywait(cor_semaphore_t *s) {
    if (s->value > 0) {
        s->value--;
        return 0;
    }
    return 1;
}

void cor_sem_post(cor_semaphore_t *s) {
    s->value++;
    if (s->waiters->length > 0) {
        make_ready(g_queue_pop_head(s->waiters));
    }
    cor_yield();
}

void cor_sem_destroy(cor_semaphore_t *s) {
    if (s->waiters->length > 0) {
        log_error("destroying using semaphore");
    }
    g_queue_free(s->waiters);
}


void cor_rwlock_init(cor_rwlock_t *rw) {
    rw->r_waiters = g_queue_new();
    rw->w_waiters = g_queue_new();
    rw->readers = 0;
    rw->writer = 0;
}

void cor_rwlock_rdlock(cor_rwlock_t *rw) {
    if (rw->writer == 0) {
        rw->readers++;
    } else if (rw->writer > 0) {
        current_coro->status = WAITING_RLOCK;
        g_queue_push_tail(rw->r_waiters, current_coro);
        cor_yield();
    }
}

int cor_rwlock_tryrdlock(cor_rwlock_t *rw) {
    if (rw->writer == 0) {
        rw->readers++;
        return 0;
    }
    return 1;
}

void cor_rwlock_wrlock(cor_rwlock_t *rw) {
    if (rw->readers == 0 && rw->writer == 0) {
        rw->writer++;
    } else {
        current_coro->status = WAITING_WLOCK;
        g_queue_push_tail(rw->w_waiters, current_coro);
        cor_yield();
    }
}

int cor_rwlock_trywrlock(cor_rwlock_t *rw) {
    if (rw->readers == 0 && rw->writer == 0) {
        rw->writer++;
        return 0;
    }
    return 1;
}

void make_ready_one(GQueue *queue) {
    make_ready(g_queue_pop_head(queue));
}

void make_ready_all(GQueue *queue) {
    while (queue->length > 0) {
        make_ready(g_queue_pop_head(queue));
    }
}

void cor_rwlock_unlock(cor_rwlock_t *rw) {
    if (rw->writer == 1) {
        rw->writer = 0;

        if (rw->r_waiters->length > 0) {
            rw->readers = (int) rw->r_waiters->length;
            make_ready_all(rw->r_waiters);
        } else if (rw->w_waiters->length > 0) {
            rw->writer = 1;
            make_ready_one(rw->w_waiters);
        }

    } else if (rw->readers > 0) {
        rw->readers--;
        if (rw->readers == 0) {

            if (rw->w_waiters->length > 0) {
                rw->writer = 1;
                make_ready_one(rw->w_waiters);
            } else {
                rw->readers = (int) rw->r_waiters->length;
                make_ready_all(rw->r_waiters);
            }

        }
    } else {
        printf("Readers %d, writers %d\n", rw->readers, rw->writer);
        log_error("Unknown error\n");
    }
    //cor_yield();
}

void cor_rwlock_destroy(cor_rwlock_t *rw) {
    g_queue_free(rw->r_waiters);
    g_queue_free(rw->w_waiters);
}


void cor_wait_on(cor_wait_token_t token) {
    cor_wait_token_info *info = g_hash_table_lookup(current_coro->scheduler->waiting_tokens, token);
    if (info == NULL) {
        info = g_new0(cor_wait_token_info, 1);
        info->waiters = g_queue_new();
        g_hash_table_insert(current_coro->scheduler->waiting_tokens, token, info);
    }
    g_queue_push_tail(info->waiters, current_coro);
    current_coro->status = WAITING_TOKEN;
    cor_yield();
}

void cor_notify_one(cor_wait_token_t token) {
    cor_wait_token_info *info = g_hash_table_lookup(current_coro->scheduler->waiting_tokens, token);
    if (info != NULL) {
        make_ready(g_queue_pop_head(info->waiters));
    }
}

void cor_notify_all(cor_wait_token_t token) {
    cor_wait_token_info *info = g_hash_table_lookup(current_coro->scheduler->waiting_tokens, token);
    if (info != NULL) {
        make_ready_all(info->waiters);
    }
}