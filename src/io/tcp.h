// system headers dependent

#ifdef _MSC_VER
#include <winsock2.h>
#include <ws2tcpip.h>
#define SHUT_RDWR SD_BOTH
#define MSG_NOSIGNAL 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

// system headers independent
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sockaddr sockaddr;

class XTcpSocket {
public:
    XTcpSocket() { sockfd = -1; }

    ~XTcpSocket(){};

    void close() {
#ifdef _MSC_VER
        closesocket(sockfd);
#else
        ::close(sockfd);
#endif
        sockfd = -1;
    }

    void abort() {
#ifdef _MSC_VER
        closesocket(sockfd);
#else
        ::close(sockfd);
#endif
        sockfd = -1;
    }

    int write(const char* buff, const int size) {
        int ret;
        // printf("writing %i\n",size);
        if ((ret = send(sockfd, buff, size, MSG_NOSIGNAL)) != size) {
#ifndef _MSC_VER
            printf("send error : %s \n", strerror(errno));
#endif
            return -1;
        }
        return ret;
    }

    int read(char* buff, int size) {
        int ret;
        // printf("reading %i ... ",size);
        // fflush(stdout);
        if ((ret = recv(sockfd, buff, size, MSG_WAITALL)) != size) {
#ifndef _MSC_VER
            printf("recv error : %s \n", strerror(errno));
#endif
            return -1;
        }
        // printf("read %i of %i\n",ret, size);
        return ret;
    }

    int isOpen(void) { return (sockfd >= 0); }

    int connectToHost(const char* host, int port) {
        struct sockaddr_in serv;

        if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
#ifndef _MSC_VER
            printf("socket error : %s \n", strerror(errno));
#endif
            return 0;
        }

        memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_addr.s_addr = inet_addr(host);
        serv.sin_port = htons(port);

        if (connect(sockfd, (sockaddr*)&serv, sizeof(serv)) < 0) {
#ifndef _MSC_VER
            printf("connect error : %s \n", strerror(errno));
#endif
            close();
            return 0;
        }

        return 1;
    }

    int socketDescriptor(void) { return sockfd; }

private:
    int sockfd;
};
