#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "web/http_server.h"
#include "misc/logger.h"


#define DEFAULT_PORT  2019   //默认端口
#define THREAD_NUM    0      //线程数量


int main(int argc, char* argv[])  
{
    int c;
    char* host = NULL;
    int port = DEFAULT_PORT;
    int thread_num = THREAD_NUM;
    int* p_port = NULL;
    int* p_thread_num = NULL;

    while ((c = getopt(argc, argv, "h:p:w:")) != -1) {
        switch (c) {
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            p_port = &port;
            break;
        case 'w':
            thread_num = atoi(optarg);
            p_thread_num = &thread_num;
            break;
        default:
            debug_quit("Usage: -h hostname -p port -w woker_thread_num\n\n");
            break;
        }
    }
   
    if (host)  {
        debug_msg("host is %s", host);
    }
    debug_msg("port, thread_num is %d, %d \n", port, thread_num);

    http_server_init();
    http_server_start(host, p_port, p_thread_num);

	return 0;
}