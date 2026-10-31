// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal POSIX declarations for PPSSPP's disabled network modules on libogc2.
// These are isolated from libogc2's net_* ABI; Wii/Network.cpp fails socket I/O.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
typedef uint32_t socklen_t;
typedef uint16_t sa_family_t;
struct in_addr { uint32_t s_addr; };
struct sockaddr { sa_family_t sa_family; char sa_data[14]; };
struct sockaddr_in { sa_family_t sin_family; uint16_t sin_port; in_addr sin_addr; char sin_zero[8]; };
struct sockaddr_storage { sa_family_t ss_family; char padding[126]; };
struct hostent { char *h_name; char **h_aliases; int h_addrtype; int h_length; char **h_addr_list; };
#define h_addr h_addr_list[0]
struct addrinfo { int ai_flags, ai_family, ai_socktype, ai_protocol; socklen_t ai_addrlen; sockaddr *ai_addr; char *ai_canonname; addrinfo *ai_next; };
struct linger { int l_onoff, l_linger; };
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 10
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define INADDR_ANY 0
#define INADDR_NONE 0xffffffffu
#define SO_REUSEADDR 2
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_ERROR 4
#define SO_SNDTIMEO 21
#define SO_RCVTIMEO 20
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000
#define SHUT_RDWR 2
#define TCP_NODELAY 1
#define AI_ADDRCONFIG 0x20
#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4
#define AI_V4MAPPED 8
#define EAI_FAIL -4
#define EAI_AGAIN -3
#define NI_NUMERICHOST 1
#define NI_NUMERICSERV 2
#define NI_MAXHOST 1025
#define INET_ADDRSTRLEN 16
#ifdef __cplusplus
extern "C" {
#endif
int getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);
struct hostent *gethostbyname(const char *);
int gethostname(char *, size_t);
int socket(int, int, int);
int connect(int, const struct sockaddr *, socklen_t);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *, socklen_t *);
int getpeername(int, struct sockaddr *, socklen_t *);
int getsockname(int, struct sockaddr *, socklen_t *);
int getsockopt(int, int, int, void *, socklen_t *);
int setsockopt(int, int, int, const void *, socklen_t);
int shutdown(int, int);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
ssize_t sendto(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom(int, void *, size_t, int, struct sockaddr *, socklen_t *);
uint32_t htonl(uint32_t);
uint32_t ntohl(uint32_t);
uint16_t htons(uint16_t);
uint16_t ntohs(uint16_t);
uint32_t inet_addr(const char *);
int inet_aton(const char *, struct in_addr *);
char *inet_ntoa(struct in_addr);
const char *inet_ntop(int, const void *, char *, socklen_t);
#ifdef __cplusplus
}
#endif
