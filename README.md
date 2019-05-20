# mwebserver
Tiny but high-performance HTTP Server, based on my [mu_event](https://github.com/shonm520/mu_event), No third-party libraries are used, only glibc. It uses the Reactor model, non-blocking IO and IO multiplexing(epoll) to handle concurrency. 

# Usage
## Build
```
git clone https://github.com/shonm520/mwebserver.git
./build.sh
```
## Run
```
./mwebserver port thread_num

e.g ./mwebserver 2019 4
```

# Benchmark

常见的压力测试工具有ab，wrk，webbench。HTTP/1.1的长连接已经很普及，wrk默认支持长连接，webbench不支持长连接测试，ab需要加上-k选项， 否则ab的压力测试会默认采用HTTP/1.0，即每一个请求建立一个TCP连接。


使用ab进行短连接测试，与nginx的对比

![](https://github.com/shonm520/mwebserver/blob/master/doc/short.png)


使用ab进行长连接测试，与nginx的对比

![](https://github.com/shonm520/mwebserver/blob/master/doc/long.png)


从ab短连接和长连接的测试结果来看，mwebserver比nginx略好，原因是mwebserver功能简单单一，没有复杂的结构。还有，可以看出长连接对比短连接的优势，连接的建立和关闭才是瓶颈所在， 因为TCP的三次握手和四次挥手比较耗费时间。

