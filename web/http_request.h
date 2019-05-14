#pragma once 
#include "http_parser.h"

typedef struct connection_t connection;

typedef struct request_t request;

struct request_t {
  connection *conn;                     /* belonged connection */
  parse_archive par;                    /* parse_archive */
  int resource_fd;                      /* resource fildes */
  int resource_size;                    /* resource size */
  int status_code;                      /* response status code */
  int (*req_handler)(request *);        /* request handler for rl, hd, bd */
  int (*res_handler)(request *);        /* response handler for hd bd */
} ;

int http_request(request*);  

void http_request_handle_init(connection* conn);

int request_reset(request *r);
//int request_handle(connection *c);

int response_handle(request *r);