#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <glib.h>
#include "../logger.h"
#include "master_listener.h"
#include "../repa_client.h"
#include "router.h"
#include <poll.h>
#include <sys/signalfd.h>
#include "net_client.h"
#include "../db/stats_service.h"
#include "../util/config.h"
#include "../util/config_extend.h"
#include "../signal_handler.h"
#include "../work/workers_pool.h"
#include <sys/eventfd.h>

#define DEFAULT_PORT 6380
void master_listener_loop(void* data);

int server_fd;
struct sockaddr_in address;

GArray *clients = NULL;
char master_buffer[1024 * 16];

long client_last_id = 0;

int event_fd;

event_cb shutdown_request_callback = NULL;

void shutdown_request_set_callback(event_cb cb) {
    shutdown_request_callback = cb;
}

GArray* to_free_clients;
pthread_mutex_t to_free_clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void on_client_execution_finished(RepaClient* client);

void init_master_listener() {
    event_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (event_fd == -1) {
        LOG(LOG_ERROR, "Error in eventfd");
        exit(EXIT_FAILURE);
    }

    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("In socket");
        exit(EXIT_FAILURE);
    }


    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    int port = get_int_config_var_or_default("port", DEFAULT_PORT, true);
    address.sin_port = htons((uint16_t) port);

    memset(address.sin_zero, '\0', sizeof address.sin_zero);


    if (bind(server_fd, (struct sockaddr *) &address, sizeof(address)) < 0) {
        perror("In bind");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("In listen");
        exit(EXIT_FAILURE);
    }

    clients = g_array_new(FALSE, TRUE, sizeof(RepaClient *));
    to_free_clients = g_array_new(FALSE, TRUE, sizeof(RepaClient*));

    set_client_executed_client(on_client_execution_finished);

    coro_create(get_default_scheduler(), master_listener_loop, NULL, 0);

    LOG(LOG_INFO, "Server started on port %d", port);
}

void stop_master_listener() {
    uint64_t trigger_val = 1;
    write(event_fd, &trigger_val, sizeof(trigger_val));
}

void register_new_client(int fd) {
    NetClient *net_client = g_new0(NetClient, 1);
    net_client->fd = fd;
    net_client->parser = resp_parser_new();

    RepaClient *client = g_new0(RepaClient, 1);
    client->net_client = net_client;

    cor_mutex_init(&client->mutex);

    client->commands_queue = g_queue_new();

    client->id = ++client_last_id;

    g_array_append_val(clients, client);
    LOG(LOG_INFO, "Client %d connected", net_client->fd);
}



#define EXT_FREE_LOG
void free_client(RepaClient* client){
    g_atomic_int_set(&client->marked_for_free, 1);

    
    int in_queue = g_atomic_int_get(&client->is_in_working_queue);
    int doing_tasks = g_atomic_int_get(&client->is_doing_tasks);

    LOG(LOG_DEBUG, "Free client %ld: in_queue=%d, doing_tasks=%d",
        client->id, in_queue, doing_tasks);

    if(in_queue || doing_tasks){
        pthread_mutex_lock(&to_free_clients_mutex);
        g_array_append_val(to_free_clients, client);
        pthread_mutex_unlock(&to_free_clients_mutex);
        EXT_FREE_LOG(LOG_INFO, "Free callback");

    }
    else{
        resp_parser_free(client->net_client->parser);
        g_queue_free(client->commands_queue);
        g_free(client->net_client);
        g_free(client);
        EXT_FREE_LOG(LOG_INFO, "Free inline begin");

    }
}
void on_client_execution_finished(RepaClient* client){
    int founded = -1;
    pthread_mutex_lock(&to_free_clients_mutex);
    for (int i = 0; i < to_free_clients->len; ++i) {
        RepaClient* to_free_client = g_array_index(to_free_clients, RepaClient*, i);
        if(to_free_client == client){
            founded = i;
            break;
        }
    }
    if(founded != -1)
        g_array_remove_index_fast(to_free_clients, founded);

    pthread_mutex_unlock(&to_free_clients_mutex);

    if(founded != -1){
        EXT_FREE_LOG(LOG_INFO, "Free callback end");
        free_client(client);
    }
}

// client not in net_clients
// require to free
void unregister_client(RepaClient *client) {
    LOG(LOG_INFO, "Client %d disconnected", client->net_client->fd);
    free_client(client);
    //g_free(client);
}

void free_clients_and_server() {
    for (int i = 0; i < clients->len; i++) {
        RepaClient *client = g_array_index(clients, RepaClient*, i);
        close(client->net_client->fd);
        unregister_client(client);
        //g_free(client);
    }
    g_array_free(clients, TRUE);

    close(server_fd);
}



void master_listener_loop(void* data) {
    while (1) {
        int reserved_fds = 3;

        // Готовим pollfds
        int nfds = (int) (reserved_fds + clients->len);
        struct pollfd *pfds = g_new0(struct pollfd, nfds);

        poll_fd_array fds;
        fds.len = nfds;
        fds.data = pfds;

        // ==== RESERVED FDS =====

        // Серверный сокет
        pfds[0].fd = server_fd;
        pfds[0].events = POLLIN;

        // Signal fd
        pfds[1].fd = get_signal_fd();
        pfds[1].events = POLLIN;

        // Event fd
        pfds[2].fd = event_fd;
        pfds[2].events = POLLIN;


        // Клиенты
        for (int i = 0; i < clients->len; i++) {
            RepaClient *client = g_array_index(clients, RepaClient*, i);
            pfds[i + reserved_fds].fd = client->net_client->fd;
            pfds[i + reserved_fds].events = POLLIN;
        }

        //printf("waiting...\n");
        int activity = cor_poll(fds);
        //LOG(LOG_INFO, "Activity detected on %d descriptor(s)", activity);

        if (activity < 0) {
            perror("poll");
            g_free(pfds);
            continue;
        }

        if (pfds[2].revents & POLLIN) {
            uint64_t val;
            read(event_fd, &val, sizeof(val));
            LOG(LOG_INFO, "Stopping server listener");
            g_free(pfds);
            free_clients_and_server();
            break;
        }

        if (pfds[1].revents & POLLIN) {
            struct signalfd_siginfo si;
            ssize_t res = read(get_signal_fd(), &si, sizeof(si));
            if (res != sizeof(si)) {
                perror("read");
                break;
            }

            if (si.ssi_signo == SIGINT) {
                LOG(LOG_INFO, "Got SIGINT (Ctrl+C)");
                goto trigger_shutdown;
            } else if (si.ssi_signo == SIGTERM) {
                LOG(LOG_INFO, "Got SIGTERM");
                goto trigger_shutdown;
            } else {
                LOG(LOG_INFO, "Got unexpected signal %d", si.ssi_signo);
                goto no_trigger_shutdown;
            }

            trigger_shutdown:
            if (shutdown_request_callback != NULL) {
                shutdown_request_callback(SHUTDOWN_REQUEST);
            }
            no_trigger_shutdown:
        }

        // Проверяем клиентов
        for (int i = 0; i < clients->len;) {
            bool to_remove = FALSE;
            RepaClient *client = g_array_index(clients, RepaClient*, i);
            int fd = client->net_client->fd;

            if (pfds[i + reserved_fds].revents & POLLIN) {
                ssize_t n = read(fd, master_buffer, sizeof(master_buffer) - 1);

                if (n > 0) {
                    size_t total_processed = 0;

                    while (total_processed < n) {
                        size_t local_processed = 0;
                        while (!resp_parser_feed(
                                client->net_client->parser,
                                master_buffer + total_processed,
                                n - total_processed,
                                &local_processed)) {
                            total_processed += local_processed;
                            if (total_processed >= n) {
                                goto not_enough_data;
                            }
                        }
                        if (local_processed == -1) { // Error while parsing
                            to_remove = TRUE;
                            break;
                        }

                        total_processed += local_processed;

                        RespData *result = resp_parser_get_result(client->net_client->parser);

                        reset_state(client->net_client->parser);

                        push_command_task(client, result);

                    }

                    not_enough_data:;
                }

                if (n <= 0 || to_remove) {
                    LOG(LOG_INFO,
                        "Client %d disconnected by %s",
                        client->id,
                        to_remove ? "server" : "client"
                    );

                    close(fd);
                    stats_add(STATS_CMD_CUR_CONN, -1);

                    g_array_remove_index_fast(clients, i);
                    unregister_client(client);

                    to_remove = true;
                }
            }

            if (!to_remove)
                i++;
        }

        // Новый клиент
        if (pfds[0].revents & POLLIN) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd >= 0) {
                register_new_client(client_fd);
                stats_add(STATS_CMD_CUR_CONN, 1);
            }
        }


        g_free(pfds);
    }
}