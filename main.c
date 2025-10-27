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

ProxyProgramArguments program_arguments = {0};

void on_shutdown_request(Event event){
    stop_db_services();
    stop_master_listener();
    stop_workers_pool();
    stop_stats_service();
    stop_master_db();
    destroy_config_table();
    fini_logger();
    //printf("All components successfully stopped\n");
}

int main(int argc, char** argv) {
    init_signal_handler();

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
    master_listener_loop();

    free(proxy_path);
    printf("Exiting from REPA SERVER\n");
    return 0;
}

// Helper для печати результата
static void print_resp_data_(const RespData *data) {
    if (!data) {
        LOG(LOG_ERROR, "NULL RespData");
        return;
    }

    switch (data->type) {
        case RESP_TYPE_STRING:
            LOG(LOG_INFO, "Simple string: %s", data->data.string_val->str);
            break;

        case RESP_TYPE_ERROR:
            LOG(LOG_INFO, "Error: %s", data->data.string_val->str);
            break;

        case RESP_TYPE_INTEGER:
            LOG(LOG_INFO, "Integer: %lld", (long long)data->data.integer_val);
            break;

        case RESP_TYPE_BULK_STRING:
            if (data->data.string_val)
                LOG(LOG_INFO, "Bulk string: \"%s\" (len=%d)",
                    data->data.string_val->str,
                    (int)data->data.string_val->len);
            else
                LOG(LOG_INFO, "Bulk string: NULL");
            break;

        case RESP_TYPE_ARRAY:
            LOG(LOG_INFO, "Array with %d elements", data->data.array_val->len);
            for (guint i = 0; i < data->data.array_val->len; i++) {
                RespData *elem = g_array_index(data->data.array_val, RespData*, i);
                print_resp_data(elem);
            }
            break;

        case RESP_TYPE_NULL:
            LOG(LOG_INFO, "Null");
            break;

        default:
            LOG(LOG_WARNING, "Unknown RESP type %d", data->type);
            break;
    }
}

static void test_case(const char *input) {
    RespParser *parser = resp_parser_new();

    LOG(LOG_INFO, "Feeding input: %s", input);

    size_t total_processed = 0;
    size_t size = strlen(input);
    size_t local_processed = 0;
    while (!resp_parser_feed(parser, input + total_processed, MIN(rand()%10+1, size - total_processed), &local_processed)){
        LOG(LOG_INFO, "Iter");
        //printf("%s\n", input + processed);
        total_processed += local_processed;
    }
    total_processed += local_processed;

    RespData *result = resp_parser_get_result(parser);
    print_resp_data(result);

    reset_state(parser);

    printf("===================\n");

    while (!resp_parser_feed(parser, input + total_processed, MIN(rand()%10+1, size - total_processed), &local_processed)){
        LOG(LOG_INFO, "Iter2");
        //printf("%s\n", input + processed);
        total_processed += local_processed;
    }

    result = resp_parser_get_result(parser);
    print_resp_data(result);
    //resp_data_free(result);

    //resp_parser_free(parser);
}

int s_main(void) {
    init_logger("repa_logs.txt", 1024 * 1024);

    // Simple string
    //test_case("+OK\r\n");

    // Integer
    //test_case("*3\r\n+123\r\n+abcdef\r\n+test string\r\n");

    // Bulk string
    //test_case("$6\r\nfoobar\r\n");

    // Null bulk string
    //test_case("$-1\r\n");

    // Array of two bulk strings
    test_case("*3\r\n$3\r\nfoo\r\n+real simple string\r\n$3\r\nbar\r\n+second command\r\n");

    // Empty array
    //test_case("*0\r\n");

    // Nested array
    //test_case("*2\r\n:1\r\n*2\r\n+OK\r\n:2\r\n");

    return 0;
}