
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

void swap_coro(coro_t *to) {
    prev_coro = current_coro;
    current_coro = to;

    if(prev_coro == NULL){
        setcontext(&to->ctx);
        return;
    }

    to->status = RUNNING;
    prev_coro->stopwatch_elapsed_prev += now_ms() - prev_coro->stopwatch_begin_cur;
    to->stopwatch_begin_cur = now_ms();
    swapcontext(&prev_coro->ctx, &to->ctx);
}

void make_ready(coro_t *coro) {
    coro->status = READY;
    g_queue_push_tail(coro->scheduler->ready_coros, coro);
}

void check_fds(coro_sched_t *sched, int timeout) {
    g_array_set_size(sched->_poll_fds, 0);
    for (int i = 0; i < sched->waiting_fds_coros->len; ++i) {
        coro_t *coro = g_array_index(sched->waiting_fds_coros, coro_t*, i);
        for (int j = 0; j < coro->waiting_fds.len; ++j) {
            struct pollfd pfd = {
                    .fd = coro->waiting_fds.data[j],
                    .events = coro->waiting_events
            };
            g_array_append_val(sched->_poll_fds, pfd);
        }
    }

    if(sched->_poll_fds->len > 0){
        poll((struct pollfd *) sched->_poll_fds->data, sched->_poll_fds->len, timeout);

        for (int i = 0; i < sched->_poll_fds->len; ++i) {
            struct pollfd pfd = g_array_index(sched->_poll_fds, struct pollfd, i);
            coro_t *waiting_coro = g_hash_table_lookup(sched->fd_to_coro, GINT_TO_POINTER(pfd.fd));
            if (pfd.revents & waiting_coro->waiting_events) {
                make_ready(waiting_coro);
                g_hash_table_remove(sched->fd_to_coro, GINT_TO_POINTER(pfd.fd));
                my_g_array_remove(sched->waiting_fds_coros, waiting_coro);
            }
        }
    }
    else if(timeout > 0){
        g_usleep(timeout * 1000);
    }
}

void check_sleeps(coro_sched_t *sched) {
    long now = now_ms();
    for (int i = 0; i < sched->waiting_sleeps_coros->len; ++i) {
        coro_t *coro = g_array_index(sched->waiting_sleeps_coros, coro_t*, i);
        if (coro->awake_time <= now) {
            make_ready(coro);
            g_array_remove_index_fast(sched->waiting_sleeps_coros, i);
            i--; // так как текущий заменяется на последний
        }
    }
}

void scheduler_coro_loop(void* arg0) {
    while (1) {
        coro_sched_t *scheduler = current_coro->scheduler;
        if (prev_coro != NULL && prev_coro != scheduler->coro) { //form other coroutine
            if (prev_coro->status == READY) {
                g_queue_push_tail(scheduler->ready_coros, current_coro);
            }
        }

        int poll_timeout = scheduler->ready_coros->length > 0 ? 0 : 100;

        check_fds(scheduler, poll_timeout);
        check_sleeps(scheduler);

        //printf("%d\n")
        if (scheduler->ready_coros->length > 0) {
            coro_t *next_coro = g_queue_pop_head(scheduler->ready_coros);
            swap_coro(next_coro);
        }
    }
}

coro_sched_t *coro_sched_create(size_t max_coroutines, size_t stack_size) {
    coro_sched_t *sched = g_new0(coro_sched_t, 1);
    sched->ready_coros = g_queue_new();
    sched->_poll_fds = g_array_new(FALSE, TRUE, sizeof(struct pollfd));
    sched->waiting_sleeps_coros = g_array_new(FALSE, TRUE, sizeof(coro_t *));
    sched->waiting_fds_coros = g_array_new(FALSE, TRUE, sizeof(coro_t *));
    sched->fd_to_coro = g_hash_table_new(g_int_hash, g_int_equal);
    sched->coro = coro_create(sched, scheduler_coro_loop, NULL, stack_size);
    return sched;
}
void cor_run(coro_sched_t *sched){
    swap_coro(sched->coro);
}

static void coro_entry_point(coro_fn_t func, void *arg) {
    coro_t *self = current_coro;
    func(arg);

    self->finished = 1;
    self->status = NONE;
    cor_yield();
}

coro_t *coro_create(coro_sched_t *sched, coro_fn_t func, void *arg, size_t stack_size) {
    coro_t *coro = g_new0(coro_t, 1);       // ты используешь GLib, nice

    coro->scheduler = sched;
    coro->status = READY;
    coro->finished = 0;
    coro->awake_time = 0;
    coro->waiting_events = 0;

    // создаём стек
    if (stack_size == 0)
        stack_size = 64 * 1024; // дефолт 64 КБ

    coro->stack = malloc(stack_size);
    assert(coro->stack && "failed to allocate stack");

    // инициализируем контекст
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = stack_size;
    if(sched->coro != NULL){
        coro->ctx.uc_link = &sched->coro->ctx;   // после завершения вернёмся в шедулер
    }


    // makecontext позволяет передать максимум int/void* аргументов
    // создаём обёртку, которая вызовет func(arg), а потом отметит завершение
    makecontext(&coro->ctx, (void (*)()) coro_entry_point, 2, func, arg);

    g_queue_push_tail(sched->ready_coros, coro);

    return coro;
}

//coro_t *coro_create(coro_sched_t *sched, coro_fn_t func, void *arg, size_t stack_size) {
//    coro_t *coro = g_new(coro_t, 1);
//    coro->status = READY;
//    coro->scheduler = sched;
//    coro->stack = g_malloc(stack_size);
//    return coro;
//    makecontext(&coro->ctx, func, 1, arg);
//}

int cor_sleep_ms(unsigned int ms) {
    current_coro->awake_time = now_ms() + ms;
    g_array_append_val(current_coro->scheduler->waiting_sleeps_coros, current_coro);
    current_coro->status = WAITING_SLEEP;
    cor_yield();
    return 1;
}


void cor_yield(void) {
    if(current_coro->status == RUNNING){
        current_coro->status = READY;
    }
    swap_coro(current_coro->scheduler->coro);
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
        g_queue_push_head(m->waiters, current_coro);
        current_coro->status = WAITING_MUTEX;
        cor_yield();
    }
}
int cor_mutex_trylock(cor_mutex_t *m){
    if (!m->locked) {
        m->locked = 1;
        m->owner = current_coro;
        return 0;
    }
    return 1;
}
void cor_mutex_destroy(cor_mutex_t *m){
    if(m->locked){
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
        make_ready(g_queue_pop_head(m->waiters));
    }
    cor_yield();
}




void cor_sem_init(cor_semaphore_t *s, int initial){
    s->value = initial;
    s->waiters = g_queue_new();
}

void cor_sem_wait(cor_semaphore_t *s){
    if(s->value > 0){
        s->value--;
    }
    else{
        g_queue_push_tail(s->waiters, current_coro);
        current_coro->status = WAITING_SEMAPHORE;
        cor_yield();
    }
}

int cor_sem_trywait(cor_semaphore_t *s){
    if(s->value > 0){
        s->value--;
        return 0;
    }
    return 1;
}

void cor_sem_post(cor_semaphore_t *s){
    s->value++;
    if(s->waiters->length > 0){
        make_ready(g_queue_pop_head(s->waiters));
    }
    cor_yield();
}

void cor_sem_destroy(cor_semaphore_t *s){
    if(s->waiters->length > 0){
        log_error("destroying using semaphore");
    }
    g_queue_free(s->waiters);
}




void cor_rwlock_init(cor_rwlock_t *rw){
    rw->r_waiters = g_queue_new();
    rw->w_waiters = g_queue_new();
    rw->readers = 0;
    rw->writer = 0;
}

void cor_rwlock_rdlock(cor_rwlock_t *rw){
    if(rw->writer == 0){
        rw->readers++;
    }
    else if(rw->writer > 0){
        current_coro->status = WAITING_RLOCK;
        g_queue_push_tail(rw->r_waiters, current_coro);
        cor_yield();
    }
}

int cor_rwlock_tryrdlock(cor_rwlock_t *rw){
    if(rw->writer == 0){
        rw->readers++;
        return 0;
    }
    return 1;
}

void cor_rwlock_wrlock(cor_rwlock_t *rw){
    if(rw->readers == 0 && rw->writer == 0){
        rw->writer++;
    }
    else{
        current_coro->status = WAITING_WLOCK;
        g_queue_push_tail(rw->w_waiters, current_coro);
        cor_yield();
    }
}

int cor_rwlock_trywrlock(cor_rwlock_t *rw){
    if(rw->readers == 0 && rw->writer == 0){
        rw->writer++;
        return 0;
    }
    return 1;
}

void make_ready_all(GQueue* queue){
    while (queue->length > 0){
        make_ready(g_queue_pop_head(queue));
    }
}

void cor_rwlock_unlock(cor_rwlock_t *rw){
    if(rw->writer == 1){
        rw->writer = 0;

        if(rw->r_waiters->length > 0){
            make_ready_all(rw->r_waiters);
        }
        else {
            make_ready_all(rw->w_waiters);
        }

    }
    else if(rw->readers > 0){
        rw->readers--;
        if(rw->readers == 0){

            if(rw->w_waiters->length > 0){
                make_ready_all(rw->w_waiters);
            }
            else {
                make_ready_all(rw->r_waiters);
            }

        }
    }
    else{
        log_error("Unknown error");
    }
    cor_yield();
}

void cor_rwlock_destroy(cor_rwlock_t *rw){
    g_queue_free(rw->r_waiters);
    g_queue_free(rw->w_waiters);
}