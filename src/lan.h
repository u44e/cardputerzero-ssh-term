/* lan.h — wired-LAN gate: the port is disabled while the app runs, and only
 * comes up (with the profile's static IP) while a session that asked for it
 * is live. On app exit the device is handed back to the OS (NetworkManager
 * re-applies its own config — DHCP or static — so nothing needs backing up). */
#ifndef SSH_TERM_LAN_H
#define SSH_TERM_LAN_H

#include "config.h"

/* App start: take every interface named by a lan-enabled profile out of
 * NetworkManager's hands and set its link down (safe to plug a cable in). */
void lan_boot_down(void);

/* Session gate: configure the profile's interface (flush + static IP + link
 * up, short carrier wait). 0 = ok or profile has no LAN config; -1 = failed
 * (caller offers "connect anyway"). Idempotent — reconnect re-runs it. */
int  lan_up(const profile_t *p);

/* Session over: back to the in-app baseline (address flushed, link down,
 * still unmanaged). Idempotent. */
void lan_session_down(void);

/* App exit: session_down + hand every boot-touched interface back to the OS
 * (nmcli managed yes; without NM the link is simply re-enabled). Idempotent. */
void lan_restore(void);

#endif /* SSH_TERM_LAN_H */
