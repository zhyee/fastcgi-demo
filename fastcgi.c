//
// Created by root on 12/26/17.
//

#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>


#define HEAD_LEN                8  //消息头长度固定为8个字节
#define BUFLEN                  4096
#define FCGI_VERSION_1           1  //版本号




// 消息类型
enum fcgi_request_type {
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
};

// 服务器希望fastcgi程序充当的角色, 这里只讨论 FCGI_RESPONDER 响应器角色
enum fcgi_role {
    FCGI_RESPONDER      = 1,
    FCGI_AUTHORIZER     = 2,
    FCGI_FILTER         = 3
};

//消息头
struct fcgi_header {
    unsigned char version;
    unsigned char type;
    unsigned char requestIdB1;
    unsigned char requestIdB0;
    unsigned char contentLengthB1;
    unsigned char contentLengthB0;
    unsigned char paddingLength;
    unsigned char reserved;
};

//请求开始发送的消息体
struct FCGI_BeginRequestBody {
    unsigned char roleB1;
    unsigned char roleB0;
    unsigned char flags;
    unsigned char reserved[5];
};

//请求结束发送的消息体
struct FCGI_EndRequestBody {
    unsigned char appStatusB3;
    unsigned char appStatusB2;
    unsigned char appStatusB1;
    unsigned char appStatusB0;
    unsigned char protocolStatus;
    unsigned char reserved[3];
};

// protocolStatus
enum protocolStatus {
    FCGI_REQUEST_COMPLETE = 0,
    FCGI_CANT_MPX_CONN = 1,
    FCGI_OVERLOADED = 2,
    FCGI_UNKNOWN_ROLE = 3
};


// 打印错误并退出
void haltError(char *type, int errnum)
{
    fprintf(stderr, "%s: %s\n", type, strerror(errnum));
    exit(EXIT_FAILURE);
}

// 存储键值对的结构体
struct paramNameValue {
    char **pname;
    char **pvalue;
    int maxLen;
    int curLen;
};




// 初始化一个键值结构体
void init_paramNV(struct paramNameValue *nv)
{
    nv->maxLen = 16;
    nv->curLen = 0;
    nv->pname = (char **)malloc(nv->maxLen * sizeof(char *));
    nv->pvalue = (char **)malloc(nv->maxLen * sizeof(char *));
}

// 扩充一个结键值构体的容量为之前的两倍
void extend_paramNV(struct paramNameValue *nv)
{
    nv->maxLen *= 2;
    nv->pname = realloc(nv->pname, nv->maxLen * sizeof(char *));
    nv->pvalue = realloc(nv->pvalue, nv->maxLen * sizeof(char *));
}

// 释放一个键值结构体
void free_paramNV(struct paramNameValue *nv)
{
    int i;

    for(i = 0; i < nv->curLen; i++)
    {
        free(nv->pname[i]);
        free(nv->pvalue[i]);
    }
    free(nv->pname);
    free(nv->pvalue);
}


// 获取指定 paramName 的值
char *getParamValue(struct paramNameValue *nv, char *paramName)
{

    int i;

    for(i = 0; i < nv->curLen; i++)
    {
        if (strncmp(paramName, nv->pname[i], strlen(paramName)) == 0)
        {
            return nv->pvalue[i];
        }
    }

    return NULL;
}


int main(){

    int servfd, connfd;
    int ret, i;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t slen, clen;

    struct fcgi_header header, headerBuf;
    struct FCGI_BeginRequestBody brBody;
    struct paramNameValue  paramNV;
    struct FCGI_EndRequestBody erBody;

    ssize_t rdlen;
    int requestId, contentLen;
    unsigned char paddingLen;
    int paramNameLen, paramValueLen;

    char buf[BUFLEN];

    unsigned char c;
    unsigned char lenbuf[3];
    char *paramName, *paramValue;

    char *htmlHead, *htmlBody;


    /*socket bind listen*/
    servfd = socket(AF_INET, SOCK_STREAM, 0);

    if (servfd == -1)
    {
        haltError("socket", errno);
    }

    slen = clen = sizeof(struct sockaddr_in);


    bzero(&servaddr, slen);

    //这里让 fastcgi程序监听 127.0.0.1:9000  和 php-fpm 监听的地址相同， 方便我们用 nginx 来测试
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

        fcntl(connfd, F_SETFL, O_NONBLOCK); // 设置socket为非阻塞

        init_paramNV(&paramNV);

        while (1) {

            //读取消息头
            bzero(&header, HEAD_LEN);
            rdlen = read(connfd, &header, HEAD_LEN);

            if (rdlen == -1)
            {
                // 无数据可读
                if (errno == EAGAIN)
                {
                    break;
                }
                else
                {
                    haltError("read", errno);
                }
            }

            if (rdlen == 0)
            {
                break; //消息读取结束
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
                    bzero(&brBody, sizeof(brBody));
                    read(connfd, &brBody, sizeof(brBody));

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
                        /*
                        FCGI_PARAMS 以键值对的方式传送，键和值之间没有'=',每个键值对之前会分别用1或4个字节来标识键和值的长度 例如：
                        \x0B\x02SERVER_PORT80\x0B\x0ESERVER_ADDR199.170.183.42
                         上面的长度是用十六进制表示的  \x0B = 11  正好为SERVER_PORT的长度， \x02 = 2 为80的长度
                        */

                        // 获取paramName的长度
                        rdlen = read(connfd, &c, 1);  //先读取一个字节，这个字节标识 paramName 的长度
                        contentLen -= rdlen;

                        if ((c & 0x80) != 0)  //如果 c 的值大于 128，则该 paramName 的长度用四个字节表示
                        {
                            rdlen = read(connfd, lenbuf, 3);
                            contentLen -= rdlen;
                            paramNameLen = ((c & 0x7f) << 24) + (lenbuf[0] << 16) + (lenbuf[1] << 8) + lenbuf[2];
                        } else
                        {
                            paramNameLen = c;
                        }

                        // 同样的方式获取paramValue的长度
                        rdlen = read(connfd, &c, 1);
                        contentLen -= rdlen;
                        if ((c & 0x80) != 0)
                        {
                            rdlen = read(connfd, lenbuf, 3);
                            contentLen -= rdlen;
                            paramValueLen = ((c & 0x7f) << 24) + (lenbuf[0] << 16) + (lenbuf[1] << 8) + lenbuf[2];
                        }
                        else
                        {
                            paramValueLen = c;
                        }

                        //读取paramName
                        paramName = (char *)calloc(paramNameLen + 1, sizeof(char));
                        rdlen = read(connfd, paramName, paramNameLen);
                        contentLen -= rdlen;

                        //读取paramValue
                        paramValue = (char *)calloc(paramValueLen + 1, sizeof(char));
                        rdlen = read(connfd, paramValue, paramValueLen);
                        contentLen -= rdlen;

                        printf("read param: %s=%s\n", paramName, paramValue);

                        if (paramNV.curLen == paramNV.maxLen)
                        {
                            // 如果键值结构体已满则把容量扩充一倍
                            extend_paramNV(&paramNV);
                        }

                        paramNV.pname[paramNV.curLen] = paramName;
                        paramNV.pvalue[paramNV.curLen] = paramValue;
                        paramNV.curLen++;

                    }

                    if (paddingLen > 0)
                    {
                        rdlen = read(connfd, buf, paddingLen);
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
                                rdlen = read(connfd, buf, BUFLEN);
                            }
                            else
                            {
                                rdlen = read(connfd, buf, contentLen);
                            }

                            contentLen -= rdlen;
                            fwrite(buf, sizeof(char), rdlen, stdout);
                        }
                        printf("\n");
                    }

                    if (paddingLen > 0)
                    {
                        rdlen = read(connfd, buf, paddingLen);
                        contentLen -= rdlen;
                    }

                    break;

                case FCGI_DATA:
                    printf("begin read data....\n");

                    if (contentLen > 0)
                    {
                        while (contentLen > 0)
                        {
                            if (contentLen > BUFLEN)
                            {
                                rdlen = read(connfd, buf, BUFLEN);
                            }
                            else
                            {
                                rdlen = read(connfd, buf, contentLen);
                            }

                            contentLen -= rdlen;
                            fwrite(buf, sizeof(char), rdlen, stdout);
                        }
                        printf("\n");
                    }

                    if (paddingLen > 0)
                    {
                        rdlen = read(connfd, buf, paddingLen);
                        contentLen -= rdlen;
                    }

                    break;

            }
        }


        /* 以上是从web服务器读取数据，下面向web服务器返回数据 */


        headerBuf.version = FCGI_VERSION_1;
        headerBuf.type = FCGI_STDOUT;

        htmlHead = "Content-type: text/html\r\n\r\n";  //响应头
        htmlBody = getParamValue(&paramNV, "SCRIPT_FILENAME");  // 把请求文件路径作为响应体返回

        printf("html: %s%s\n",htmlHead, htmlBody);

        contentLen = strlen(htmlHead) + strlen(htmlBody);

        headerBuf.contentLengthB1 = (contentLen >> 8) & 0xff;
        headerBuf.contentLengthB0 = contentLen & 0xff;
        headerBuf.paddingLength = (contentLen % 8) > 0 ? 8 - (contentLen % 8) : 0;  // 让数据 8 字节对齐


        write(connfd, &headerBuf, HEAD_LEN);
        write(connfd, htmlHead, strlen(htmlHead));
        write(connfd, htmlBody, strlen(htmlBody));

        if (headerBuf.paddingLength > 0)
        {
            write(connfd, buf, headerBuf.paddingLength);  //填充数据随便写什么，数据会被服务器忽略
        }

        free_paramNV(&paramNV);

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
