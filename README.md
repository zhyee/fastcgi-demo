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

```
