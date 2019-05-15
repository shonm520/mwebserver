#include <stdio.h>
#include "web/http_server.h"


#define DEFAULT_PORT  2019   //默认端口
#define THREAD_NUM    0      //线程数量


int main(int argc, char* argv[])  
{

    int port = DEFAULT_PORT;
    int thread_num = THREAD_NUM;
    int* p_port = NULL;
    int* p_thread_num = NULL;
    if (argc >= 2)  {
        port = atoi(argv[1]);
        p_port = &port;
    }
    if (argc >= 3)  {
        thread_num = atoi(argv[2]);
        p_thread_num = &thread_num;
    }
  
    debug_msg("port, thread_num is %d, %d \n", port, thread_num);

    http_server_init();
    http_server_start(p_port, p_thread_num);

	return 0;
}