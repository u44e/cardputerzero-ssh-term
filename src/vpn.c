/* vpn.c — VPN bring-up/tear-down + readiness probe.
 * Exec path is device-only (pkexec + wg-quick); the getifaddrs probe runs
 * everywhere. Generic command-based design so any VPN tool can be slotted in. */
#include "vpn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <net/if.h>

static int  s_we_started = 0;
static char s_name[64] = {0};

int vpn_is_up(void)
{
    struct ifaddrs *ifa, *list;
    if (getifaddrs(&list) != 0) return 0;
    int up = 0;
    for (ifa = list; ifa; ifa = ifa->ifa_next) {
        const char *n = ifa->ifa_name;
        if (!n) continue;
        if (!strncmp(n, "wg", 2) || !strncmp(n, "tun", 3) || !strncmp(n, "utun", 4)) {
            if (ifa->ifa_flags & IFF_UP) { up = 1; break; }
        }
    }
    freeifaddrs(list);
    return up;
}

/* run argv synchronously; returns child exit code, or -1 */
static int run(const char *const argv[])
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid) return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int vpn_up(const char *name)
{
    if (!name || !*name) return 0;     /* no VPN configured */
    if (vpn_is_up()) { s_we_started = 0; return 0; }  /* already up */

    /* device: bring up via pkexec wg-quick. (openvpn/tailscale variants can be
     * selected from the vpn config block in a later pass.) */
    snprintf(s_name, sizeof(s_name), "%s", name);
    const char *argv[] = { "pkexec", "wg-quick", "up", name, NULL };
    int rc = run(argv);
    if (rc == 0) { s_we_started = 1; return vpn_is_up() ? 0 : 0; }
    return -1;   /* caller offers "connect anyway / cancel" */
}

void vpn_down(void)
{
    if (!s_we_started || !s_name[0]) return;
    const char *argv[] = { "pkexec", "wg-quick", "down", s_name, NULL };
    run(argv);
    s_we_started = 0;
    s_name[0] = 0;
}
