// SPDX-License-Identifier: GPL-2.0-or-later
// Deliberately fail unsupported networking rather than pretending it succeeded.
#include <cerrno>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

extern "C" {
int getaddrinfo(const char *, const char *, const addrinfo *, addrinfo **result) {
  if (result) *result = nullptr;
  return EAI_FAIL;
}
void freeaddrinfo(addrinfo *) {}
const char *gai_strerror(int) { return "Networking is unavailable in this Wii port"; }
int gethostname(char *, size_t) { errno = ENOSYS; return -1; }
int getpeername(int, sockaddr *, socklen_t *) { errno = ENOSYS; return -1; }
int select(int, fd_set *, fd_set *, fd_set *, timeval *) { errno = ENETDOWN; return -1; }
int socket(int, int, int) { errno = ENETDOWN; return -1; }
int connect(int, const sockaddr *, socklen_t) { errno = ENETDOWN; return -1; }
int bind(int, const sockaddr *, socklen_t) { errno = ENETDOWN; return -1; }
int listen(int, int) { errno = ENETDOWN; return -1; }
int accept(int, sockaddr *, socklen_t *) { errno = ENETDOWN; return -1; }
int getsockname(int, sockaddr *, socklen_t *) { errno = ENETDOWN; return -1; }
int getsockopt(int, int, int, void *, socklen_t *) { errno = ENETDOWN; return -1; }
int setsockopt(int, int, int, const void *, socklen_t) { errno = ENETDOWN; return -1; }
int shutdown(int, int) { errno = ENETDOWN; return -1; }
ssize_t send(int, const void *, size_t, int) { errno = ENETDOWN; return -1; }
ssize_t recv(int, void *, size_t, int) { errno = ENETDOWN; return -1; }
ssize_t sendto(int, const void *, size_t, int, const sockaddr *, socklen_t) { errno = ENETDOWN; return -1; }
ssize_t recvfrom(int, void *, size_t, int, sockaddr *, socklen_t *) { errno = ENETDOWN; return -1; }
hostent *gethostbyname(const char *) { errno = ENETDOWN; return nullptr; }
uint32_t htonl(uint32_t v) { return v; }
uint32_t ntohl(uint32_t v) { return v; }
uint16_t htons(uint16_t v) { return v; }
uint16_t ntohs(uint16_t v) { return v; }
uint32_t inet_addr(const char *) { return INADDR_NONE; }
int inet_aton(const char *, in_addr *) { return 0; }
char *inet_ntoa(in_addr) { static char text[] = "0.0.0.0"; return text; }
const char *inet_ntop(int, const void *, char *, socklen_t) { errno = ENETDOWN; return nullptr; }
}
