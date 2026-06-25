/* vpn.h — bring a VPN up before connecting, tear it down after.
 * Uses pkexec + standard tools on device (wg-quick/openvpn/tailscale).
 * Readiness is probed via getifaddrs (no root needed). */
#ifndef SSH_TERM_VPN_H
#define SSH_TERM_VPN_H

/* Bring up a VPN of `type` (wireguard|openvpn|ikev2|l2tp|tailscale) using `cfg`
 * (config name/path, or connection name for ipsec). Returns 0 if up, -1 on fail.
 * No-op (returns 0) when type is empty or "none". */
int  vpn_up(const char *type, const char *cfg);

/* Selectable VPN types (index 0 = "none"). */
extern const char *const VPN_TYPES[];
int  vpn_type_count(void);

/* Tear down only if WE brought it up. */
void vpn_down(void);

/* 1 if a VPN interface (wg / tun / utun) is present. */
int  vpn_is_up(void);

#endif /* SSH_TERM_VPN_H */
