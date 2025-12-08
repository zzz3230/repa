#include <glib.h>
#include <stdio.h>
#include <libgen.h>
#include "logger.h"
#include "network/master_listener.h"
#include "work/workers_pool.h"
#include "resp_parser.h"
#include "db/db_backend.h"
#include "db/stats_service.h"
#include "util/config.h"
#include "signal_handler.h"
#include "db/db_services.h"
#include "coroutine_pool/coroutine_pool.h"
#include "default_coro.h"

ProxyProgramArguments program_arguments = {0};

void on_shutdown_request(Event event){
    stop_db_services();
    stop_workers_pool();
    stop_master_listener();
    stop_stats_service();
    stop_master_db();
    destroy_config_table();
    fini_logger();
}

int main(int argc, char** argv) {
    init_signal_handler();

    init_default_scheduler();

    init_logger("repa_logs.txt", 1024 * 1024);

    char* proxy_path = calloc(1, strlen(argv[0]) + 1);
    if(proxy_path == NULL){
        fprintf(stderr, "Failed to allocate memory for proxy path\n");
        return 1;
    }
    strcpy(proxy_path, argv[0]);
    program_arguments.proxy_path = dirname(proxy_path);

    create_config_table();
    init_config(program_arguments);

    init_master_db();
    init_stats_service();
    init_workers_pool();
    init_master_listener();
    start_db_services();

    shutdown_request_set_callback(on_shutdown_request);
    //master_listener_loop();
    cor_run(get_default_scheduler());

    free(proxy_path);
    printf("Exiting from REPA SERVER\n");
    return 0;
}
