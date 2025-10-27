//
// Created by zzz32 on 9/28/2025.
//

#include <unistd.h>
#include "net_client.h"

void send_response_to_client(NetClient* client, char* data, size_t len){
    ssize_t written = write(client->fd, data, len);
    fsync(client->fd);

    if((size_t)written != len){
        LOG(LOG_ERROR, "Written length dismatch");
    }
}