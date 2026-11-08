#include "bus.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

static int open_socketcan(vecu_bus_t *b, const char *ifname)
{
  struct ifreq ifr;
  struct sockaddr_can addr;
  const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    perror("socket(PF_CAN)");
    return -1;
  }
  memset(&ifr, 0, sizeof ifr);
  snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    perror("SIOCGIFINDEX");
    close(fd);
    return -1;
  }
  memset(&addr, 0, sizeof addr);
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
    perror("bind(can)");
    close(fd);
    return -1;
  }
  (void)fcntl(fd, F_SETFL, O_NONBLOCK);
  b->fd = fd;
  b->kind = 1;
  return 0;
}

static void vbus_ctl(vecu_bus_t *b, unsigned char kind)
{
  unsigned char d[16];
  memset(d, 0, sizeof d);
  d[5] = kind;
  (void)sendto(b->fd, d, sizeof d, 0, (struct sockaddr *)b->peer, b->peer_len);
}

static int open_vbus(vecu_bus_t *b, const char *hostport)
{
  struct sockaddr_in a;
  char host[64];
  const char *colon = strrchr(hostport, ':');
  int fd;
  if ((colon == NULL) || ((size_t)(colon - hostport) >= sizeof host)) {
    fprintf(stderr, "vbus spec must be vbus:<host>:<port>\n");
    return -1;
  }
  memcpy(host, hostport, (size_t)(colon - hostport));
  host[colon - hostport] = '\0';
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)atoi(colon + 1));
  if (inet_pton(AF_INET, host, &a.sin_addr) != 1) {
    fprintf(stderr, "bad vbus host %s\n", host);
    return -1;
  }
  fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket(udp)");
    return -1;
  }
  (void)fcntl(fd, F_SETFL, O_NONBLOCK);
  memcpy(b->peer, &a, sizeof a);
  b->peer_len = sizeof a;
  b->fd = fd;
  b->kind = 2;
  vbus_ctl(b, 1u);
  return 0;
}

int vecu_bus_open(vecu_bus_t *b, const char *spec)
{
  memset(b, 0, sizeof *b);
  b->fd = -1;
  if (strncmp(spec, "socketcan:", 10) == 0) {
    return open_socketcan(b, spec + 10);
  }
  if (strncmp(spec, "vbus:", 5) == 0) {
    return open_vbus(b, spec + 5);
  }
  fprintf(stderr, "unknown bus %s (socketcan:<if> or vbus:<host>:<port>)\n", spec);
  return -1;
}

int vecu_bus_send(vecu_bus_t *b, const vecu_frame_t *f)
{
  if (b->kind == 1) {
    struct can_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = f->id;
    cf.can_dlc = f->dlc;
    memcpy(cf.data, f->data, 8u);
    return (write(b->fd, &cf, sizeof cf) == (ssize_t)sizeof cf) ? 0 : -1;
  } else {
    unsigned char d[16];
    memset(d, 0, sizeof d);
    d[0] = (unsigned char)(f->id & 0xFFu);
    d[1] = (unsigned char)((f->id >> 8) & 0xFFu);
    d[2] = (unsigned char)((f->id >> 16) & 0xFFu);
    d[3] = (unsigned char)((f->id >> 24) & 0xFFu);
    d[4] = f->dlc;
    memcpy(&d[8], f->data, 8u);
    return (sendto(b->fd, d, sizeof d, 0, (struct sockaddr *)b->peer, b->peer_len) == 16) ? 0 : -1;
  }
}

int vecu_bus_recv(vecu_bus_t *b, vecu_frame_t *f)
{
  if (b->kind == 1) {
    struct can_frame cf;
    const ssize_t n = read(b->fd, &cf, sizeof cf);
    if (n < 0) {
      return ((errno == EAGAIN) || (errno == EWOULDBLOCK)) ? 0 : -1;
    }
    if ((n != (ssize_t)sizeof cf) || ((cf.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG)) != 0u)) {
      return 0;
    }
    f->id = cf.can_id & CAN_SFF_MASK;
    f->dlc = (cf.can_dlc > 8u) ? 8u : cf.can_dlc;
    memset(f->data, 0, 8u);
    memcpy(f->data, cf.data, f->dlc);
    return 1;
  } else {
    unsigned char d[16];
    const ssize_t n = recv(b->fd, d, sizeof d, 0);
    if (n < 0) {
      return ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == ECONNREFUSED)) ? 0 : -1;
    }
    if ((n != 16) || (d[5] != 0u)) {
      return 0;
    }
    f->id = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
    f->dlc = (d[4] > 8u) ? 8u : d[4];
    memcpy(f->data, &d[8], 8u);
    return 1;
  }
}

void vecu_bus_keepalive(vecu_bus_t *b)
{
  if (b->kind == 2) {
    vbus_ctl(b, 1u);
  }
}

void vecu_bus_close(vecu_bus_t *b)
{
  if (b->fd >= 0) {
    if (b->kind == 2) {
      vbus_ctl(b, 2u);
    }
    close(b->fd);
    b->fd = -1;
  }
}
