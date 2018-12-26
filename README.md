# fastcgi-demo
实现fastcgi协议的一个小demo

## build

```shell
git clone https://github.com/zhyee/fastcgi-demo.git
cd fastcgi-demo
make
```

## 测试
1. 由于这个demo监听和php-fpm相同的端口，所以先要停止php-fpm
2. 执行 `./fastcgi` 运行起来，此时程序处于阻塞等待连接状态
3. 另开一个窗口，curl 随意发起一个php请求，这个php文件不需要存在，例如:
 `curl -d "name=tom&age=27" http://127.0.0.1/fastcgi.php?type=test`
4. 查看curl返回和fastcgi程序的输出，正常应该能够看到这样的输出：
```
version = 1, type = 1, requestId = 1, contentLen = 8, paddingLength = 0
80001000101
******************************* begin request *******************************
role = 1, flags = 0
version = 1, type = 4, requestId = 1, contentLen = 604, paddingLength = 4
45c0201000401
begin read params...
read param: SCRIPT_FILENAME=/tmp/www/fastcgi.php
read param: QUERY_STRING=type=test
read param: REQUEST_METHOD=POST
read param: CONTENT_TYPE=application/x-www-form-urlencoded
read param: CONTENT_LENGTH=15
read param: SCRIPT_NAME=/fastcgi.php
read param: REQUEST_URI=/fastcgi.php?type=test
read param: DOCUMENT_URI=/fastcgi.php
read param: DOCUMENT_ROOT=/tmp/www
read param: SERVER_PROTOCOL=HTTP/1.1
read param: REQUEST_SCHEME=http
read param: GATEWAY_INTERFACE=CGI/1.1
read param: SERVER_SOFTWARE=nginx/1.12.2
read param: REMOTE_ADDR=127.0.0.1
read param: REMOTE_PORT=44162
read param: SERVER_ADDR=127.0.0.1
read param: SERVER_PORT=80
read param: SERVER_NAME=_
read param: REDIRECT_STATUS=200
read param: HTTP_USER_AGENT=curl/7.29.0
read param: HTTP_HOST=127.0.0.1
read param: HTTP_ACCEPT=*/*
read param: HTTP_CONTENT_LENGTH=15
read param: HTTP_CONTENT_TYPE=application/x-www-form-urlencoded
version = 1, type = 4, requestId = 1, contentLen = 0, paddingLength = 0
1000401
begin read params...
read params end...
version = 1, type = 5, requestId = 1, contentLen = 15, paddingLength = 1
10f0001000501
begin read post...
name=tom&age=27
version = 1, type = 5, requestId = 1, contentLen = 0, paddingLength = 0
1000501
begin read post...
read post end....
html: Content-type: text/html

/tmp/www/fastcgi.php
******************************* end request *******************************
```
