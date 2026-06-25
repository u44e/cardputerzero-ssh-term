/* vpn.h — bring a VPN up before connecting, tear it down after.
 * Uses pkexec + standard tools on device (wg-quick/openvpn/tailscale).
 * Readiness is probed via getifaddrs (no root needed). */
#ifndef SSH_TERM_VPN_H
#define SSH_TERM_VPN_H

/* Bring up VPN profile `name` (a wg-quick config name by default).
 * Returns 0 if it is (or became) up, -1 on failure. No-op if name is empty. */
int  vpn_up(const char *name);

/* Tear down only if WE brought it up. */
void vpn_down(void);

/* 1 if a VPN interface (wg / tun / utun) is present. */
int  vpn_is_up(void);

#endif /* SSH_TERM_VPN_H */
