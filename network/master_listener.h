//
// Created by zzz32 on 9/13/2025.
//

#ifndef REPA_SERVER_MASTER_LISTENER_H
#define REPA_SERVER_MASTER_LISTENER_H

#include <glib.h>
#include "../resp_parser.h"

typedef enum e_event{
    SHUTDOWN_REQUEST
} Event;

typedef void (*event_cb)(Event event_type);

void init_master_listener();
void master_listener_loop();
void stop_master_listener();
void shutdown_request_set_callback(event_cb cb);

#endif //REPA_SERVER_MASTER_LISTENER_H
