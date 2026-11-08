#include "bus.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
/* cppcheck-suppress misra-c2012-21.6 ; DEV-01 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define BUS_SOCKETCAN 1
#define BUS_VBUS 2
#define VBUS_LEN 16u

/* The BSD socket API takes every address as 'struct sockaddr *', so the two casts the adapter needs
 * live here. MISRA deviation DEV-02 (rule 11.3); cppcheck 2.13 does not report struct-to-struct
 * pointer casts, so this deviation is recorded by review, not by a suppression. */
static const struct sockaddr *sa_of_can(const struct sockaddr_can *a) { return (const struct sockaddr *)a; }

static const struct sockaddr *sa_of_in(const struct sockaddr_in *a) { return (const struct sockaddr *)a; }

static int open_socketcan(vecu_bus_t *b, const char *ifname)
{
  struct ifreq ifr;
  struct sockaddr_can addr;
  int rc = -1;
  const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
  if (fd < 0) {
    perror("socket(PF_CAN)");
  } else {
    (void)memset(&ifr, 0, sizeof ifr);
    (void)snprintf(ifr.ifr_name, sizeof ifr.ifr_name, "%s", ifname);
    (void)memset(&addr, 0, sizeof addr);
    /* cppcheck-suppress misra-c2012-17.3 ; FP-01 */
    /* cppcheck-suppress misra-config ; FP-01 */
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
      perror("SIOCGIFINDEX");
    } else {
      addr.can_family = AF_CAN;
      addr.can_ifindex = ifr.ifr_ifindex;
      if (bind(fd, sa_of_can(&addr), sizeof addr) < 0) {
        perror("bind(can)");
      } else {
        (void)fcntl(fd, F_SETFL, O_NONBLOCK);
        b->fd = fd;
        b->kind = BUS_SOCKETCAN;
        rc = 0;
      }
    }
    if (rc != 0) {
      (void)close(fd);
    }
  }
  return rc;
}

static void vbus_ctl(const vecu_bus_t *b, uint8_t kind)
{
  uint8_t d[VBUS_LEN];
  (void)memset(d, 0, sizeof d);
  d[5] = kind;
  (void)sendto(b->fd, d, sizeof d, 0, sa_of_in(&b->peer), sizeof b->peer);
}

/* Splits "<host>:<port>" at the last colon into host and a port number 1..65535; 0 on success. */
static int parse_hostport(const char *hostport, char *host, size_t host_size, uint16_t *port)
{
  int rc = -1;
  const size_t len = strlen(hostport);
  size_t colon = len;
  size_t i;
  for (i = 0u; i < len; ++i) {
    if (hostport[i] == ':') {
      colon = i;
    }
  }
  if ((colon < len) && (colon < host_size)) {
    char *end = NULL;
    unsigned long v;
    errno = 0;
    v = strtoul(&hostport[colon + 1u], &end, 10);
    if ((errno == 0) && (end != &hostport[colon + 1u]) && (*end == '\0') && (v >= 1u) && (v <= 65535u)) {
      (void)memcpy(host, hostport, colon);
      host[colon] = '\0';
      *port = (uint16_t)v;
      rc = 0;
    }
  }
  return rc;
}

static int open_vbus(vecu_bus_t *b, const char *hostport)
{
  char host[64];
  uint16_t port = 0u;
  int rc = -1;
  (void)memset(&b->peer, 0, sizeof b->peer);
  if (parse_hostport(hostport, host, sizeof host, &port) != 0) {
    (void)fprintf(stderr, "vbus spec must be vbus:<host>:<port>\n");
  } else if (inet_pton(AF_INET, host, &b->peer.sin_addr) != 1) {
    (void)fprintf(stderr, "bad vbus host %s\n", host);
  } else {
    /* cppcheck-suppress misra-c2012-17.3 ; FP-01 */
    /* cppcheck-suppress misra-config ; FP-01 */
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
      perror("socket(udp)");
    } else {
      (void)fcntl(fd, F_SETFL, O_NONBLOCK);
      b->peer.sin_family = AF_INET;
      b->peer.sin_port = htons(port);
      b->fd = fd;
      b->kind = BUS_VBUS;
      vbus_ctl(b, 1u);
      rc = 0;
    }
  }
  return rc;
}

int vecu_bus_open(vecu_bus_t *b, const char *spec)
{
  int rc;
  (void)memset(b, 0, sizeof *b);
  b->fd = -1;
  if (strncmp(spec, "socketcan:", 10u) == 0) {
    rc = open_socketcan(b, &spec[10]);
  } else if (strncmp(spec, "vbus:", 5u) == 0) {
    rc = open_vbus(b, &spec[5]);
  } else {
    (void)fprintf(stderr, "unknown bus %s (socketcan:<if> or vbus:<host>:<port>)\n", spec);
    rc = -1;
  }
  return rc;
}

int vecu_bus_send(const vecu_bus_t *b, const vecu_frame_t *f)
{
  int rc;
  if (b->kind == BUS_SOCKETCAN) {
    struct can_frame cf;
    (void)memset(&cf, 0, sizeof cf);
    cf.can_id = f->id;
    cf.can_dlc = f->dlc;
    (void)memcpy(cf.data, f->data, 8u);
    rc = -1;
    if (write(b->fd, &cf, sizeof cf) == (ssize_t)sizeof cf) {
      rc = 0;
    }
  } else {
    uint8_t d[VBUS_LEN];
    (void)memset(d, 0, sizeof d);
    d[0] = (uint8_t)(f->id & 0xFFu);
    d[1] = (uint8_t)((f->id >> 8) & 0xFFu);
    d[2] = (uint8_t)((f->id >> 16) & 0xFFu);
    d[3] = (uint8_t)((f->id >> 24) & 0xFFu);
    d[4] = f->dlc;
    (void)memcpy(&d[8], f->data, 8u);
    rc = -1;
    if (sendto(b->fd, d, sizeof d, 0, sa_of_in(&b->peer), sizeof b->peer) == (ssize_t)VBUS_LEN) {
      rc = 0;
    }
  }
  return rc;
}

static bool would_block(int err) { return (err == EAGAIN) || (err == EWOULDBLOCK); }

int vecu_bus_recv(const vecu_bus_t *b, vecu_frame_t *f)
{
  int rc = 0;
  if (b->kind == BUS_SOCKETCAN) {
    struct can_frame cf;
    ssize_t n;
    bool data_frame;
    (void)memset(&cf, 0, sizeof cf);
    n = read(b->fd, &cf, sizeof cf);
    /* cppcheck-suppress misra-config ; FP-01 */
    data_frame = (cf.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG)) == 0u;
    if (n < 0) {
      if (!would_block(errno)) {
        rc = -1;
      }
    } else if ((n == (ssize_t)sizeof cf) && data_frame) {
      f->id = cf.can_id & CAN_SFF_MASK;
      f->dlc = (cf.can_dlc > 8u) ? 8u : cf.can_dlc;
      (void)memset(f->data, 0, 8u);
      (void)memcpy(f->data, cf.data, f->dlc);
      rc = 1;
    } else {
      /* short read, error or remote frame: skip */
    }
  } else {
    uint8_t d[VBUS_LEN];
    const ssize_t n = recv(b->fd, d, sizeof d, 0);
    if (n < 0) {
      if (!would_block(errno) && (errno != ECONNREFUSED)) {
        rc = -1;
      }
    } else if ((n == (ssize_t)VBUS_LEN) && (d[5] == 0u)) {
      f->id = (uint32_t)d[0] | ((uint32_t)d[1] << 8) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
      f->dlc = (d[4] > 8u) ? 8u : d[4];
      (void)memcpy(f->data, &d[8], 8u);
      rc = 1;
    } else {
      /* control datagram or wrong size: skip */
    }
  }
  return rc;
}

void vecu_bus_keepalive(const vecu_bus_t *b)
{
  if (b->kind == BUS_VBUS) {
    vbus_ctl(b, 1u);
  }
}

void vecu_bus_close(vecu_bus_t *b)
{
  if (b->fd >= 0) {
    if (b->kind == BUS_VBUS) {
      vbus_ctl(b, 2u);
    }
    (void)close(b->fd);
    b->fd = -1;
  }
}
