#include <stdio.h>
#include <signal.h>
#include "mevent/servermanager.h"
#include "mevent/connection.h"
#include "mevent/listener.h"
#include "mevent/buffer.h"
#include "mevent/config.h"

#include "config.h"

#include "web/http_request.h"


config server_config;

int g_msgcnt = 0;
int g_concnt = 0;

void onMessage(connection *conn)
{
    
    int len = ring_buffer_readable_bytes(conn->ring_buffer_read);
    debug_msg("onMessage!!!! %d, fd is %d\n", len, conn->connfd);
    char* msg = ring_buffer_readable_start(conn->ring_buffer_read);

    debug_msg("msg is %s\n", msg);
    g_msgcnt++;
    http_request(conn->handler);  
}

void onConnection(connection* conn)
{
    debug_msg("connected!!!! fd is %d\n", conn->connfd);
    g_concnt++;
    http_request_handle_init(conn);
}

void stop(int stop)
{
    printf("\ng_concnt, g_msgcnt is %d, %d\n", g_concnt, g_msgcnt);
    signal(SIGINT, SIG_DFL);
}

int main(int argc, char* argv[])  
{
    signal(SIGINT, stop);
    
    int port = DEFAULT_PORT;
    int thread_num = MAX_LOOP;
    if (argc >= 2)
        port = atoi(argv[1]);
    if (argc >= 3)
        thread_num = atoi(argv[2]);
  
    debug_msg("port, thread_num is %d, %d \n", port, thread_num);

    config_parse("", &server_config);

    mime_dict_init();
    header_handler_dict_init();
    status_table_init();

	server_manager *manager = server_manager_create(port, thread_num);
	inet_address addr = addr_create("any", port);
	listener_create(manager, addr, onMessage, onConnection);
	server_manager_run(manager);

	return 0;
}