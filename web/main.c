#include <stdio.h>
#include "mevent/servermanager.h"
#include "mevent/connection.h"
#include "mevent/listener.h"
#include "mevent/buffer.h"
#include "mevent/config.h"

#include "web/http.h"
#include "config.h"


config server_config;


void onMessage(connection *conn)
{
    
    int len = ring_buffer_readable_bytes(conn->ring_buffer_read);
    printf("onMessage!!!! %d, fd is %d\n", len, conn->connfd);
    char* msg = ring_buffer_readable_start(conn->ring_buffer_read);

    printf("msg is %s\n", msg);

   
    //do_request(msg, len); 

    http_request(conn->handler);  
}

void onConnection(connection* conn)
{
    printf("connected!!!! fd is %d\n", conn->connfd);

    http_request_handle_init(conn);

}

int main(int argc, char* argv[])  
{
     int port = DEFAULT_PORT;
    int thread_num = MAX_LOOP;
    if (argc >= 2)
        port = atoi(argv[1]);
    if (argc >= 3)
        thread_num = atoi(argv[2]);
  
    printf("port, thread_num is %d, %d \n", port, thread_num);


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