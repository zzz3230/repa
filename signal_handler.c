//
// Created by zzz32 on 10/24/2025.
//

#include <signal.h>
#include "signal_handler.h"
#include "logger.h"
#include <sys/signalfd.h>
#include <stdio.h>
#include <stdlib.h>


int signal_fd = -1;

int get_signal_fd(){
    return signal_fd;
}

void init_signal_handler() {
    // Bind SIGINT, SIGTERM
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    // Blocking
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1) {
        printf("ERROR While SIG_BLOCK");
        exit(EXIT_FAILURE);
    }

    // Creating fd
    int sfd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (sfd == -1) {
        printf("ERROR While signalfd");
        exit(EXIT_FAILURE);
    }

    signal_fd = sfd;
}