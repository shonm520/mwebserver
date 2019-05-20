# mwebserver
tiny but high-performance HTTP Server, based on my [mu_event](https://github.com/shonm520/mu_event), No third-party libraries are used, only glibc.


# Benchmark


使用ab进行短连接测试，与nginx的对比

![](https://github.com/shonm520/mwebserver/blob/master/doc/short.png)


使用ab进行长连接测试，与nginx的对比

![](https://github.com/shonm520/mwebserver/blob/master/doc/long.png)




