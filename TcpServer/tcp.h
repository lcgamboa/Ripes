// system headers dependent

#ifdef WIN
#include <winsock2.h>
#include <ws2tcpip.h>
WORD wVersionRequested = 2;
WSADATA wsaData;
#define SHUT_RDWR SD_BOTH
#define MSG_NOSIGNAL 0
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#endif

// system headers independent
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// defines
typedef struct sockaddr sockaddr;
