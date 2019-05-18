#pragma once

typedef struct {
    int timeout_keep_alive;      // notify client connection timeout_keep_alive in header
    int connect_time_limit;      // Connection: close when the connection is over time
    char *rootdir;               // html root directory 
    int rootdir_fd;              // fildes of rootdir 
    int port;
    int work_thread;
} config;

int config_parse(char* file, config*);