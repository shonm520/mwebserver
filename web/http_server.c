
#include "mevent/servermanager.h"
#include "mevent/listener.h"
#include "mevent/connection.h"
#include "mevent/ring_buffer.h"
#include "mevent/config.h"
#include "web/config.h"
#include "web/http_request.h"
#include "web/http_response.h"

#include <stdio.h>
#include <sys/time.h>


config server_config;


static void onMessage(connection *conn)           //in worker thread if has, and run after onConnection
{
    //int len = 0;
    //char* msg = ring_buffer_get_msg(conn->ring_buffer_read, &len);
    //debug_msg("fd: %d, port: %d msg is %s\n", conn->connfd, conn->port, msg);

    http_request(conn->handler);  
}

static void onDisconnected(connection* conn)
{
    //debug_msg("onDisconnected : %d\n", conn->connfd);
    request* req = (request*)conn->handler;
    free(req);
}

static void onConnection(connection* conn)       //in main thread
{
    //debug_msg("connected!!!! fd is %d\n", conn->connfd);
    http_request_handle_init(conn);

    connection_set_disconnect_callback(conn, onDisconnected);
}

void http_server_init()
{
    mime_dict_init();
    header_handler_dict_init();
    status_table_init();

    config_parse("", &server_config);
}


void http_server_start(char* p_host, int* p_port, int* p_work_thread)
{
    char* host = (p_host ? p_host : "any");
    int port = (p_port ? *p_port : server_config.port);
    int work_thread = (p_work_thread ? *p_work_thread : server_config.work_thread);

    server_manager *manager = server_manager_create(port, work_thread);
	inet_address addr = addr_create(host, port);
	listener_create(manager, addr, onMessage, onConnection);
	server_manager_run(manager);
}