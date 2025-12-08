#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/poll.h>
#include "coroutine_pool.h"

// Вспомогательные функции для тестов
static int test_counter = 0;
static int test_failed = 0;

#define TEST_START(name) \
    printf("TEST: %s\n", name); \
    test_counter++;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s at %s:%d\n", msg, __FILE__, __LINE__); \
            test_failed++; \
        } else { \
            printf("  PASS: %s\n", msg); \
        } \
    } while(0)

#define TEST_END() \
    printf("----------------------------------------\n")

// Глобальные переменные для передачи данных между корутинами в тестах
static int global_test_result = 0;
static int global_test_counter = 0;
static void* global_test_data = NULL;

// 1. Тест создания и базового выполнения корутин
static void coro_test_simple(void* arg) {
    int* result = (int*)arg;
    *result = 42;
    cor_yield();
    *result = 100;
    cor_exit();
}

static void test_basic_creation() {
    TEST_START("Basic coroutine creation and execution");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int result = 0;
    coro_t* coro = coro_create(sched, coro_test_simple, &result, 0);
    TEST_CHECK(coro != NULL, "Coroutine created");
    TEST_CHECK(cor_count_total(sched) == 1, "Total count is 1");
    TEST_CHECK(cor_count_ready(sched) == 1, "Ready count is 1");

    cor_run(sched);
    TEST_CHECK(result == 100, "Coroutine executed fully");
    TEST_CHECK(cor_count_total(sched) == 0, "All coroutines finished");

    coro_sched_destroy(sched);

    TEST_END();
}

int idx = 0;

// 2. Тест нескольких корутин и порядка выполнения
static void coro_order_a(void* arg) {
    int* order_check = (int*)arg;
    order_check[idx++] = 1;
    cor_yield();
    order_check[idx++] = 3;

}

static void coro_order_b(void* arg) {
    int* order_check = (int*)arg;
    order_check[idx++] = 2;

    cor_yield();
    order_check[idx++] = 4;

    cor_yield();
    order_check[idx++] = 5;
}

static void test_multiple_coroutines() {
    TEST_START("Multiple coroutines and execution order");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int order_check[10] = {0};
    global_test_data = order_check;

    coro_create(sched, coro_order_a, order_check, 0);
    coro_create(sched, coro_order_b, order_check, 0);

    TEST_CHECK(cor_count_total(sched) == 2, "Two coroutines created");

    cor_run(sched);

    // Проверяем порядок выполнения
    TEST_CHECK(order_check[0] == 1, "First coro starts");
    TEST_CHECK(order_check[1] == 2, "Second coro starts");
    TEST_CHECK(order_check[2] == 3, "First coro continues");
    TEST_CHECK(order_check[3] == 4, "Second coro continues");
    TEST_CHECK(order_check[4] == 5, "Second coro finishes");

    coro_sched_destroy(sched);

    TEST_END();
}

// 3. Тест взаимной блокировки с yield
static void coro_mutual_a(void* arg) {
    int* shared = (int*)arg;
    for (int i = 0; i < 5; i++) {
        (*shared)++;
        cor_yield();
    }
}

static void coro_mutual_b(void* arg) {
    int* shared = (int*)arg;
    for (int i = 0; i < 5; i++) {
        (*shared) *= 2;
        cor_yield();
    }
}

static void test_mutual_yield() {
    TEST_START("Mutual yielding with shared state");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int shared = 1;
    coro_create(sched, coro_mutual_b, &shared, 0);
    coro_create(sched, coro_mutual_a, &shared, 0);

    cor_run(sched);

    // 1 * 2 + 1 = 3, *2 = 6, +1 = 7, *2 = 14, +1 = 15, *2 = 30, +1 = 31, *2 = 62, +1 = 63
    TEST_CHECK(shared == 63, "Shared state correctly updated");

    coro_sched_destroy(sched);

    TEST_END();
}

// 4. Тест мьютексов - обертка для безопасного тестирования
typedef struct {
    cor_mutex_t* mutex;
    int* counter;
    int iterations;
} mutex_test_data;

static void coro_mutex_test(void* arg) {
    mutex_test_data* data = (mutex_test_data*)arg;

    cor_mutex_lock(data->mutex);
    for (int i = 0; i < data->iterations; i++) {
        (*(data->counter))++;
        cor_yield();
    }
    cor_mutex_unlock(data->mutex);
}

static void test_mutex() {
    TEST_START("Mutex synchronization");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    cor_mutex_t mutex;
    int counter1 = 0;
    int counter2 = 0;

    // Инициализируем мьютекс вне корутин (это разрешено)
    cor_mutex_init(&mutex);

    // Тестируем безопасность из корутин
    mutex_test_data data1 = {&mutex, &counter1, 100};
    mutex_test_data data2 = {&mutex, &counter2, 100};

    coro_create(sched, coro_mutex_test, &data1, 0);
    coro_create(sched, coro_mutex_test, &data2, 0);

    cor_run(sched);

    TEST_CHECK(counter1 == 100, "Counter1 updated correctly");
    TEST_CHECK(counter2 == 100, "Counter2 updated correctly");

    // Проверяем что мьютекс разблокирован
    mutex_test_data data3 = {&mutex, &counter1, 1};
    coro_create(sched, coro_mutex_test, &data3, 0);
    cor_run(sched);
    TEST_CHECK(counter1 == 101, "Mutex can be reused");

    cor_mutex_destroy(&mutex);
    coro_sched_destroy(sched);

    TEST_END();
}

// 5. Тест семафоров
typedef struct {
    cor_semaphore_t* sem;
    int* counter;
} sem_test_data;

static void coro_sem_consumer(void* arg) {
    sem_test_data* data = (sem_test_data*)arg;
    cor_sem_wait(data->sem);
    (*(data->counter))++;
}

static void coro_sem_producer(void* arg) {
    sem_test_data* data = (sem_test_data*)arg;
    cor_sem_post(data->sem);
}



// Даем ему время начать ждать
static void coro_delayed_producer(void* arg) {
    sem_test_data* data = (sem_test_data*)arg;
    cor_yield(); // Даем потребителю запуститься
    cor_yield();
    cor_sem_post(data->sem);
}

// Производитель который добавит еще ресурсов
static void coro_multi_producer(void* arg) {
    sem_test_data* data = (sem_test_data*)arg;
    for (int i = 0; i < 3; i++) {
        cor_sem_post(data->sem);
        cor_yield();
    }
}
static void test_semaphore() {
    TEST_START("Semaphore synchronization");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    cor_semaphore_t sem;
    int counter = 0;

    // Инициализация семафора разрешена вне корутин
    cor_sem_init(&sem, 0);

    // Тест с несколькими потребителями
    cor_sem_init(&sem, 2); // Два ресурса доступно

    for (int i = 0; i < 3; i++) {
        sem_test_data data = {&sem, &counter};
        coro_create(sched, coro_sem_consumer, &data, 0);
    }

    sem_test_data multi_data = {&sem, &counter};
    coro_create(sched, coro_multi_producer, &multi_data, 0);

    cor_run(sched);

    TEST_CHECK(counter == 3, "All consumers got resources");

    cor_sem_destroy(&sem);
    coro_sched_destroy(sched);

    TEST_END();
}

// 6. Тест read-write блокировок
typedef struct {
    cor_rwlock_t* rwlock;
    int* readers_count;
    int* writer_active;
    int is_writer;
} rwlock_test_data;

static void coro_rwlock_test(void* arg) {
    rwlock_test_data* data = (rwlock_test_data*)arg;

    if (data->is_writer) {
        cor_rwlock_wrlock(data->rwlock);
        (*(data->writer_active)) = 1;
        TEST_CHECK(*(data->readers_count) == 0, "No readers while writer holds lock");

        // Имитируем запись
        for (int i = 0; i < 3; i++) {
            cor_yield();
        }

        (*(data->writer_active)) = 0;
        cor_rwlock_unlock(data->rwlock);
    } else {
        cor_rwlock_rdlock(data->rwlock);
        (*(data->readers_count))++;
        TEST_CHECK(*(data->writer_active) == 0, "No writer while readers hold lock");

        // Имитируем чтение
        for (int i = 0; i < 2; i++) {
            cor_yield();
        }

        (*(data->readers_count))--;
        cor_rwlock_unlock(data->rwlock);
    }
}

static void test_rwlock() {
    TEST_START("Read-Write lock synchronization");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    cor_rwlock_t rwlock;
    int readers_count = 0;
    int writer_active = 0;

    cor_rwlock_init(&rwlock);

    // Создаем несколько читателей
    for (int i = 0; i < 3; i++) {
        rwlock_test_data data = {&rwlock, &readers_count, &writer_active, 0};
        coro_create(sched, coro_rwlock_test, &data, 0);
    }

    // Создаем писателя
    rwlock_test_data writer_data = {&rwlock, &readers_count, &writer_active, 1};
    coro_create(sched, coro_rwlock_test, &writer_data, 0);

    cor_run(sched);

    TEST_CHECK(readers_count == 0, "All readers finished");
    TEST_CHECK(writer_active == 0, "Writer finished");

    cor_rwlock_destroy(&rwlock);
    coro_sched_destroy(sched);

    TEST_END();
}

// 7. Тест сна и таймаутов
typedef struct {
    int ms;
    int* result;
} sleep_test_data;

static void coro_sleep_test(void* arg) {
    sleep_test_data* data = (sleep_test_data*)arg;
    int ret = cor_sleep_ms(data->ms);
    *(data->result) = ret;
}

static void test_sleep() {
    TEST_START("Sleep and timeout functionality");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int result1 = -1, result2 = -1;
    sleep_test_data data1 = {100, &result1}; // Короткий сон
    sleep_test_data data2 = {500, &result2}; // Длинный сон

    // Создаем корутину с долгим сном
    coro_create(sched, coro_sleep_test, &data2, 0);

    // Создаем корутину с коротким сном
    coro_create(sched, coro_sleep_test, &data1, 0);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    cor_run(sched);

    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 +
                      (end.tv_nsec - start.tv_nsec) / 1000000;

    TEST_CHECK(result1 == 1, "First sleep completed (timeout)");
    TEST_CHECK(result2 == 1, "Second sleep completed (timeout)");
    TEST_CHECK(elapsed_ms >= 45, "Enough time elapsed for sleeps");

    coro_sched_destroy(sched);

    TEST_END();
}

// 8. Тест wait/notify токенов
typedef struct {
    cor_wait_token_t token;
    int* woken_count;
} wait_test_data;

static void coro_wait_test(void* arg) {
    wait_test_data* data = (wait_test_data*)arg;
    cor_wait_on(data->token);
    (*(data->woken_count))++;
}

static void coro_notify_test(void* arg) {
    wait_test_data* data = (wait_test_data*)arg;
    cor_yield(); // Даем всем заснуть
    cor_yield();

    cor_notify_one(data->token);
    cor_yield();
    cor_yield();

    cor_notify_all(data->token);
}

static void test_wait_notify() {
    TEST_START("Wait/Notify token system");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int woken_count = 0;
    cor_wait_token_t token = (cor_wait_token_t)&token; // Любой уникальный указатель

    // Создаем несколько ожидающих
    for (int i = 0; i < 3; i++) {
        wait_test_data data = {token, &woken_count};
        coro_create(sched, coro_wait_test, &data, 0);
    }

    // Создаем нотифаера
    wait_test_data notifier_data = {token, &woken_count};
    coro_create(sched, coro_notify_test, &notifier_data, 0);

    cor_run(sched);

    TEST_CHECK(woken_count == 3, "All waiters were woken");

    coro_sched_destroy(sched);

    TEST_END();
}

// 9. Тест poll (ожидание на файловых дескрипторах)
typedef struct {
    int read_fd;
    int write_fd;
} pipe_test_data;

static void coro_poll_reader(void* arg) {
    pipe_test_data* data = (pipe_test_data*)arg;
    struct pollfd fds[1];
    fds[0].fd = data->read_fd;
    fds[0].events = POLLIN;
    fds[0].revents = 0;

    poll_fd_array fd_array = {fds, 1};

    int ret = cor_poll(fd_array);
    if (ret > 0 && (fds[0].revents & POLLIN)) {
        char buf[10];
        read(data->read_fd, buf, sizeof(buf));
        global_test_result = 1; // Успех
    }
}

static void coro_poll_writer(void* arg) {
    pipe_test_data* data = (pipe_test_data*)arg;

    // Даем читателю начать ждать
    cor_yield();
    cor_yield();

    // Пишем в pipe
    write(data->write_fd, "test", 5);
}

static void test_poll() {
    TEST_START("Poll on file descriptors");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int pipe_fds[2];
    int pipe_ret = pipe(pipe_fds);
    TEST_CHECK(pipe_ret == 0, "Pipe created");

    if (pipe_ret != 0) {
        coro_sched_destroy(sched);
        return;
    }

    // Делаем неблокирующим для безопасности
    fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_fds[1], F_SETFL, O_NONBLOCK);

    global_test_result = 0;
    pipe_test_data data = {pipe_fds[0], pipe_fds[1]};

    coro_create(sched, coro_poll_reader, &data, 0);
    coro_create(sched, coro_poll_writer, &data, 0);

    cor_run(sched);

    close(pipe_fds[0]);
    close(pipe_fds[1]);

    TEST_CHECK(global_test_result == 1, "Poll worked correctly");

    coro_sched_destroy(sched);

    TEST_END();
}



// Создаем дочернюю корутину
static void coro_child(void* arg) {
    int* child_result = (int*)arg;
    *child_result = 20;
    cor_yield();
    *child_result = 30;
}
// 10. Тест вложенных корутин
static void coro_parent(void* arg) {
    int* result = (int*)arg;
    *result = 10;



    int child_result = 0;
    coro_t* child = coro_create(coro_current()->scheduler, coro_child, &child_result, 0);
    TEST_CHECK(child != NULL, "Child coroutine created from parent");

    // Ждем завершения дочерней корутины
    while (cor_count_total(coro_current()->scheduler) > 1) {
        cor_yield();
    }

    TEST_CHECK(child_result == 30, "Child coroutine executed");
    *result = child_result + 5;
}

static void test_nested_coroutines() {
    TEST_START("Nested coroutine creation");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    int result = 0;
    coro_create(sched, coro_parent, &result, 0);

    cor_run(sched);

    TEST_CHECK(result == 35, "Parent and child executed correctly");
    TEST_CHECK(cor_count_total(sched) == 0, "All coroutines finished");

    coro_sched_destroy(sched);

    TEST_END();
}

// 11. Стресс-тест
typedef struct {
    cor_mutex_t* mutex;
    int* counter;
    int id;
} stress_test_data;

static void coro_stress(void* arg) {
    stress_test_data* data = (stress_test_data*)arg;

    for (int i = 0; i < 50; i++) {
        cor_mutex_lock(data->mutex);
        (*(data->counter))++;
        cor_mutex_unlock(data->mutex);

        cor_yield();

        cor_mutex_lock(data->mutex);
        (*(data->counter))--;
        cor_mutex_unlock(data->mutex);

        cor_yield();
    }
}

static void test_stress() {
    TEST_START("Stress test with many coroutines");

    coro_sched_t* sched = coro_sched_create(200, 8 * 1024); // Меньший стек для стресс-теста
    TEST_CHECK(sched != NULL, "Scheduler created");

    cor_mutex_t mutex;
    cor_mutex_init(&mutex);

    int counter = 0;

    // Создаем много корутин
    for (int i = 0; i < 20; i++) {
        stress_test_data* data = malloc(sizeof(stress_test_data));
        data->mutex = &mutex;
        data->counter = &counter;
        data->id = i;

        coro_create(sched, coro_stress, data, 0);
    }

    cor_run(sched);

    TEST_CHECK(counter == 0, "Counter balanced correctly after stress test");
    TEST_CHECK(cor_count_total(sched) == 0, "All coroutines finished");

    // Освобождаем память (в реальном тесте нужно было бы отслеживать указатели)
    cor_mutex_destroy(&mutex);
    coro_sched_destroy(sched);

    TEST_END();
}




// Тест 2: Пустой poll
static void coro_empty_poll(void* arg) {
    poll_fd_array empty_fds = {NULL, 0};
    int poll_result = cor_poll(empty_fds);
    TEST_CHECK(poll_result == 0, "Empty poll returns immediately");
}
// Создаем одну корутину и завершаем
static void coro_dummy(void* arg) {
    // Ничего не делаем
}
// Тест 3: Очень маленький стек
static void coro_small_stack(void* arg) {
    int x = 42;
    cor_yield();
    TEST_CHECK(x == 42, "Small stack works");
}

// 12. Тест ошибок и граничных случаев
static void test_error_cases() {
    TEST_START("Error and edge cases");

    // Тест 1: Создание корутины с нулевым шедулером
    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");

    // Создание корутины с нулевой функцией
    coro_t* bad_coro = coro_create(sched, NULL, NULL, 0);
    TEST_CHECK(bad_coro == NULL, "Cannot create coroutine with NULL function");



    coro_create(sched, coro_empty_poll, NULL, 0);

    // Создаем с очень маленьким стеком (может упасть на некоторых системах)
//    coro_t* small_stack_coro = coro_create(sched, coro_small_stack, NULL, 256);
//    if (small_stack_coro) {
//        TEST_CHECK(1, "Coroutine with small stack created");
//    }

    cor_run(sched);

    coro_sched_destroy(sched);

    // Тест 4: Инициализация/завершение без использования
    coro_sched_t sched2;
    int init_result = coro_sched_init(&sched2, 50, 16 * 1024);
    TEST_CHECK(init_result == 0, "Scheduler initialized successfully");



    coro_create(&sched2, coro_dummy, NULL, 0);
    cor_run(&sched2);
    coro_sched_shutdown(&sched2);

    TEST_END();
}



static void coro_stopwatch_test(void* arg) {
    long* results = (long*)arg;

    coro_stopwatch_start();
    for (int i = 0; i < 1000; i++) {
        // busy wait
        volatile int x = i * i;
    }
    results[0] = coro_stopwatch_elapsed();

    cor_yield();

    coro_stopwatch_start();
    for (int i = 0; i < 5000; i++) {
        volatile int x = i * i;
    }
    results[1] = coro_stopwatch_elapsed();

    TEST_CHECK(results[0] >= 0, "First measurement valid");
    TEST_CHECK(results[1] >= 0, "Second measurement valid");
    TEST_CHECK(results[1] >= results[0], "Second task took longer");
}

// 13. Тест stopwatch функций
static void test_stopwatch() {
    TEST_START("Stopwatch functionality");

    coro_sched_t* sched = coro_sched_create(100, 16 * 1024);
    TEST_CHECK(sched != NULL, "Scheduler created");


    long results[2] = {0};
    coro_create(sched, coro_stopwatch_test, results, 0);
    cor_run(sched);

    coro_sched_destroy(sched);

    TEST_END();
}

// Главная функция тестов
int ______main() {
    printf("========================================\n");
    printf("Coroutine Pool Tests\n");
    printf("========================================\n\n");

    // Запускаем все тесты
    test_basic_creation();
    test_multiple_coroutines();
    test_mutual_yield();
    test_mutex();
    test_semaphore();
    test_rwlock();
    test_sleep();
    test_wait_notify();
    test_poll();
    test_nested_coroutines();
    test_stress();
    test_error_cases();
    test_stopwatch();

    // Выводим статистику
    printf("\n========================================\n");
    printf("Test Summary:\n");
    printf("  Total tests: %d\n", test_counter);
    printf("  Failed tests: %d\n", test_failed);

    if (test_failed == 0) {
        printf("  RESULT: ALL TESTS PASSED\n");
    } else {
        printf("  RESULT: %d TESTS FAILED\n", test_failed);
    }

    printf("========================================\n");

    return test_failed > 0 ? 1 : 0;
}