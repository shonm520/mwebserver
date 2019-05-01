#pragma once

typedef struct {
  int timeout;     /* connection expired time */
  char *rootdir;   /* html root directory */
  int rootdir_fd;  /* fildes of rootdir */
} config;

int config_parse(char* file, config*);