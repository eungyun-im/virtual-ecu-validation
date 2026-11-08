/* Bus access layer. The ECU core never touches the bus; the runtime uses one of two backends:
 *   socketcan:<ifname>   Linux SocketCAN (vcan0 on a laptop or a CI runner that can load vcan)
 *   vbus:<host>:<port>   UDP virtual bus relayed by tools/vecu/vbus_broker.py, for machines
 *                        without vcan. Same frames, same timing loop.
 * vbus datagram (16 bytes, little-endian): u32 id | u8 dlc | u8 kind (0 frame, 1 hello, 2 bye) | u16 0 | data[8] */
#ifndef VECU_BUS_H
#define VECU_BUS_H

#include <netinet/in.h>

#include "vecu/vecu_types.h"

typedef struct {
  int fd;
  int kind;                /* 1 socketcan, 2 vbus */
  struct sockaddr_in peer; /* vbus broker */
} vecu_bus_t;

int vecu_bus_open(vecu_bus_t *b, const char *spec);
int vecu_bus_send(const vecu_bus_t *b, const vecu_frame_t *f);
/* Non-blocking read: 1 = frame, 0 = none, -1 = error */
int vecu_bus_recv(const vecu_bus_t *b, vecu_frame_t *f);
void vecu_bus_keepalive(const vecu_bus_t *b);
void vecu_bus_close(vecu_bus_t *b);

#endif
