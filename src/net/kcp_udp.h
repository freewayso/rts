#ifndef KCP_UDP_H
#define KCP_UDP_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool KcpUdp_Listen(uint16_t port);
bool KcpUdp_Connect(const char *ip, uint16_t port);
void KcpUdp_Shutdown(void);
void KcpUdp_Poll(void);

int  KcpUdp_Recv(uint8_t *buf, int max);
int  KcpUdp_RecvFrom(int *peer, uint8_t *buf, int max);
bool KcpUdp_SendTo(int peer, const uint8_t *buf, int len);
bool KcpUdp_Broadcast(const uint8_t *buf, int len);
int  KcpUdp_PeerCount(void);
bool KcpUdp_IsHost(void);

#endif
