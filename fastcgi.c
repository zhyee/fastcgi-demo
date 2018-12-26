//
// Created by root on 12/26/17.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "fastcgi.h"


#define HEAD_LEN                8  //消息头长度固定为8个字节
#define BUFLEN                  4096
#define FCGI_VERSION_1           1  //版本号


// 消息类型
typedef enum fcgi_request_type {
    FCGI_BEGIN_REQUEST      = 1,
    FCGI_ABORT_REQUEST      = 2,
    FCGI_END_REQUEST        = 3,
    FCGI_PARAMS             = 4,
    FCGI_STDIN              = 5,
    FCGI_STDOUT             = 6,
    FCGI_STDERR             = 7,
    FCGI_DATA               = 8,
    FCGI_GET_VALUES         = 9,
    FCGI_GET_VALUES_RESULT  = 10,
    FCGI_UNKOWN_TYPE        = 11
} fcgi_request_type;

// 服务器希望fastcgi程序充当的角色, 这里只讨论 FCGI_RESPONDER 响应器角色
typedef enum fcgi_role {
    FCGI_RESPONDER      = 1,
    FCGI_AUTHORIZER     = 2,
    FCGI_FILTER         = 3
} fcgi_role;

//消息头
typedef struct fcgi_header {
    unsigned char version;
    unsigned char type;
    unsigned char requestIdB1;
    unsigned char requestIdB0;
    unsigned char contentLengthB1;
    unsigned char contentLengthB0;
    unsigned char paddingLength;
    unsigned char reserved;
} fcgi_header;

//请求开始发送的消息体
typedef struct FCGI_BeginRequestBody {
    unsigned char roleB1;
    unsigned char roleB0;
    unsigned char flags;
    unsigned char reserved[5];
} FCGI_BeginRequestBody;

//请求结束发送的消息体
typedef struct FCGI_EndRequestBody {
    unsigned char appStatusB3;
    unsigned char appStatusB2;
    unsigned char appStatusB1;
    unsigned char appStatusB0;
    unsigned char protocolStatus;
    unsigned char reserved[3];
} FCGI_EndRequestBody;

// protocolStatus
typedef enum protocolStatus {
    FCGI_REQUEST_COMPLETE = 0,
    FCGI_CANT_MPX_CONN = 1,
    FCGI_OVERLOADED = 2,
    FCGI_UNKNOWN_ROLE = 3
} protocolStatus;

// 存储键值对的结构体
typedef struct paramNameValue {
    unsigned int cap;  // 能存储的键值对容量
    unsigned int len;    // 当前已存储的键值对数量
    char *param[0];  //使用柔性数组  "name=value" 格式
} paramNameValue;

typedef struct bufStream {
    int readLen;
    int renderPos;
    char buf[BUFLEN];
} bufStream;


// 打印错误并退出
void haltError(const char *prefix, int errerno)
{
    fprintf(stderr, "%s: %s\n", prefix, strerror(errerno));
    exit(EXIT_FAILURE);
}

static int readNext(int fd, bufStream *bs)
{
    bs->renderPos = 0;
    bzero(bs->buf, BUFLEN);
    bs->readLen = read(fd, bs->buf, BUFLEN);
    return bs->readLen;
}

static int renderNext(int fd, char *buf, int bufLen, bufStream *bs)
{
    int remain = bufLen;
    while (remain > 0)
    {
        bufLen = remain;

        while (bs->readLen <= bs->renderPos)
        {
            readNext(fd, bs);
            if (bs->readLen == -1)
            {
                if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN) {
                    return 0;  // 数据已经读完
                } else {
                    return -1; //读取出错
                }
            }
            if (bs->readLen == 0)
            {
                return 0;  // EOF
            }
        }

        if (bufLen > (bs->readLen - bs->renderPos))
        {
            bufLen = bs->readLen - bs->renderPos;
        }

        memcpy(buf, bs->buf + bs->renderPos, bufLen);
        bs->renderPos += bufLen;
        remain -= bufLen;
    }
    return bufLen;
}

// 初始化一个键值结构体
static paramNameValue *create_paramNV(unsigned int cap)
{
    // 容量最小为8，并且2的整数次幂对齐
    unsigned int size = 8;
    while (size < cap) {
        size <<= 1;
    }

    paramNameValue *nv = malloc(sizeof(paramNameValue) + sizeof(char *) * size);
    nv->cap = size;
    nv->len = 0;
    return nv;
}

// 扩充一个键值结构体的容量为之前的两倍
static paramNameValue *extend_paramNV(paramNameValue *nv)
{
    nv->cap <<= 1;
    nv = realloc(nv, sizeof(paramNameValue) + nv->cap * sizeof(char *));
    return nv;
}

// 释放一个键值结构体
static void free_paramNV(paramNameValue *nv)
{
    int i;

    for(i = 0; i < nv->len; i++)
    {
        free(nv->param[i]);
    }
    free(nv);
}


// 获取指定 paramName 的值
static const char *getParamValue(paramNameValue *nv, const char *paramName)
{

    int i;
    for(i = 0; i < nv->len; i++)
    {
        if (nv->param[i][strlen(paramName)] == '=' && strncmp(paramName, nv->param[i], strlen(paramName)) == 0)
        {
            return nv->param[i] + strlen(paramName) + 1;
        }
    }

    return NULL;
}


int main(int argc, char *args[]){

    int servfd, connfd, ret, i;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t slen, clen;

    fcgi_header header, headerBuf;
    FCGI_BeginRequestBody brBody;
    FCGI_EndRequestBody erBody;
    paramNameValue *paramNV;
    bufStream bs;

    int renderRet, rdlen, requestId;
	unsigned int contentLen, paramNameLen, paramValueLen;
    unsigned char paddingLen;

    char buf[BUFLEN];

    unsigned char c;
    unsigned char lenbuf[3];
    char *param,
         *htmlHead,
         *htmlBody;


    /*socket bind listen*/
    servfd = socket(AF_INET, SOCK_STREAM, 0);

    if (servfd == -1)
    {
        haltError("socket", errno);
    }

    slen = clen = sizeof(struct sockaddr_in);


    bzero(&servaddr, slen);

    /**
     * 这里让程序监听 127.0.0.1:9000,  和 php-fpm 监听相同的端口， 方便我们用 nginx 来配合测试
     */
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(9000);
    servaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    ret = bind(servfd, (struct sockaddr *)&servaddr, slen);

    if (ret == -1)
    {
        haltError("bind", errno);
    }

    ret = listen(servfd, 16);

    if (ret == -1)
    {
        haltError("listen", errno);
    }


    while (1)
    {
        bzero(&cliaddr, clen);
        connfd = accept(servfd, (struct sockaddr *)&cliaddr, &clen);
        if (connfd == -1)
        {
            haltError("accept", errno);
            break;
        }

        fcntl(connfd, F_SETFL, O_NONBLOCK);

        paramNV = create_paramNV(16);

        while (1) {
            //读取消息头
            renderRet = renderNext(connfd, (char *)&header, HEAD_LEN, &bs);

            if (renderRet <= 0) {
                break;
            }

            headerBuf = header;
            requestId = (header.requestIdB1 << 8) + header.requestIdB0;
            contentLen = (header.contentLengthB1 << 8) + header.contentLengthB0;
            paddingLen = header.paddingLength;


            printf("version = %d, type = %d, requestId = %d, contentLen = %d, paddingLength = %d\n",
                   header.version, header.type, requestId, contentLen, paddingLen);

            printf("%lx\n", header);


            switch (header.type) {

                case FCGI_BEGIN_REQUEST:
                    printf("******************************* begin request *******************************\n");

                    //读取开始请求的请求体
                    renderNext(connfd, (char *)&brBody, sizeof(brBody), &bs);
                    printf("role = %d, flags = %d\n", (brBody.roleB1 << 8) + brBody.roleB0, brBody.flags);
                    break;

                case FCGI_PARAMS:
                    printf("begin read params...\n");

                    // 消息头中的contentLen = 0 表明此类消息已发送完毕
                    if (contentLen == 0)
                    {
                        printf("read params end...\n");
                    }

                    //循环读取键值对
                    while (contentLen > 0)
                    {

                        /**
                        * FCGI_PARAMS 以键值对的方式传送，键和值之间没有'=',每个键值对之前会分别用1或4个字节来标识键和值的长度
                        * 例如: \x0B\x02SERVER_PORT80\x0B\x0ESERVER_ADDR199.170.183.42
                        * 上面的长度是十六进制的  \x0B = 11  正好为字符串 "SERVER_PORT" 的长度， \x02 = 2 为字符串 "80" 的长度
                        */

                        //先读取一个字节，这个字节标识 paramName 的长度
                        rdlen = renderNext(connfd, &c, 1, &bs);
                        contentLen -= rdlen;

                        if ((c & 0x80) != 0)  //如果 c 的值大于 128，则该 paramName 的长度用四个字节表示
                        {
                            rdlen = renderNext(connfd, lenbuf, 3, &bs);
                            contentLen -= rdlen;
                            paramNameLen = ((c & 0x7f) << 24) + (lenbuf[0] << 16) + (lenbuf[1] << 8) + lenbuf[2];
                        } else
                        {
                            paramNameLen = c;
                        }

                        // 同样的方式获取paramValue的长度
                        rdlen = renderNext(connfd, &c, 1, &bs);
                        contentLen -= rdlen;
                        if ((c & 0x80) != 0)
                        {
                            rdlen = renderNext(connfd, lenbuf, 3, &bs);
                            contentLen -= rdlen;
                            paramValueLen = ((c & 0x7f) << 24) + (lenbuf[0] << 16) + (lenbuf[1] << 8) + lenbuf[2];
                        }
                        else
                        {
                            paramValueLen = c;
                        }

                        //读取paramName
                        param = (char *)calloc(paramNameLen + paramValueLen + 2, sizeof(char));
                        rdlen = renderNext(connfd, param, paramNameLen, &bs);
                        contentLen -= rdlen;

                        param[paramNameLen] = '=';  // 用等号拼接

                        //读取paramValue
                        rdlen = renderNext(connfd, param + paramNameLen + 1, paramValueLen, &bs);
                        contentLen -= rdlen;

                        printf("read param: %s\n", param);

                        if (paramNV->len == paramNV->cap)
                        {
                            // 如果键值结构体已满则把容量扩充一倍
                            paramNV = extend_paramNV(paramNV);
                        }

                        paramNV->param[paramNV->len++] = param;
                    }

                    if (paddingLen > 0)
                    {
                        rdlen = renderNext(connfd, buf, paddingLen, &bs);
                        contentLen -= rdlen;
                    }

                    break;

                case FCGI_STDIN:
                    printf("begin read post...\n");

                    if(contentLen == 0)
                    {
                        printf("read post end....\n");
                    }

                    if (contentLen > 0)
                    {
                        while (contentLen > 0)
                        {
                            if (contentLen > BUFLEN)
                            {
                                rdlen = renderNext(connfd, buf, BUFLEN, &bs);
                            }
                            else
                            {
                                rdlen = renderNext(connfd, buf, contentLen, &bs);
                            }

                            contentLen -= rdlen;
                            fwrite(buf, sizeof(char), rdlen, stdout);
                        }
                        printf("\n");
                    }

                    if (paddingLen > 0)
                    {
                        rdlen = renderNext(connfd, buf, paddingLen, &bs);
                        contentLen -= rdlen;
                    }

                    break;

            }
        }


        /* 以上是从web服务器读取数据，下面向web服务器返回数据 */


        headerBuf.version = FCGI_VERSION_1;
        headerBuf.type = FCGI_STDOUT;

        htmlHead = "Content-type: text/html\r\n\r\n";  //响应头
        htmlBody = (char *)getParamValue(paramNV, "SCRIPT_FILENAME");  // 把请求文件路径作为响应体返回

        printf("html: %s%s\n",htmlHead, htmlBody);

        contentLen = strlen(htmlHead) + strlen(htmlBody);

        headerBuf.contentLengthB1 = (contentLen >> 8) & 0xff;
        headerBuf.contentLengthB0 = contentLen & 0xff;
        headerBuf.paddingLength = (contentLen & 7) > 0 ? (8 - (contentLen & 7)) : 0;  // 填充数据让数据8字节对齐


        write(connfd, &headerBuf, HEAD_LEN);
        write(connfd, htmlHead, strlen(htmlHead));
        write(connfd, htmlBody, strlen(htmlBody));

        if (headerBuf.paddingLength > 0)
        {
            write(connfd, buf, headerBuf.paddingLength);  //填充数据随便写什么，数据会被对端忽略
        }

        free_paramNV(paramNV);

        //回写一个空的 FCGI_STDOUT 表明 该类型消息已发送结束
        headerBuf.type = FCGI_STDOUT;
        headerBuf.contentLengthB1 = 0;
        headerBuf.contentLengthB0 = 0;
        headerBuf.paddingLength = 0;
        write(connfd, &headerBuf, HEAD_LEN);


        // 发送结束请求消息头
        headerBuf.type = FCGI_END_REQUEST;
        headerBuf.contentLengthB1 = 0;
        headerBuf.contentLengthB0 = 8;
        headerBuf.paddingLength = 0;

        bzero(&erBody, sizeof(erBody));
        erBody.protocolStatus = FCGI_REQUEST_COMPLETE;

        write(connfd, &headerBuf, HEAD_LEN);
        write(connfd, &erBody, sizeof(erBody));

        close(connfd);

        printf("******************************* end request *******************************\n");
    }

    close(servfd);

    return 0;
}
