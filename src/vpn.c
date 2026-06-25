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

/* iPhone-style selectable VPN types (Linux backends in comments) */
const char *const VPN_TYPES[] = {
    "none",
    "wireguard",   /* wg-quick                */
    "openvpn",     /* openvpn --config        */
    "ikev2",       /* strongSwan: ipsec up    */
    "l2tp",        /* xl2tpd + IPsec          */
    "tailscale",   /* tailscale up            */
};
int vpn_type_count(void) { return (int)(sizeof(VPN_TYPES) / sizeof(VPN_TYPES[0])); }

static int  s_we_started = 0;
static char s_type[16] = {0};
static char s_cfg[64]  = {0};

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

int vpn_up(const profile_t *p)
{
    const char *type = p ? p->vpn_type : "";
    if (!type || !*type || !strcmp(type, "none")) return 0;   /* no VPN */
    if (vpn_is_up()) { s_we_started = 0; return 0; }          /* already up */

    /* Each type uses the fields its editor showed. wg-quick/openvpn take a config;
     * ikev2/l2tp use a pre-defined connection (Server/User/Pass are stored for the
     * device to generate ipsec/xl2tpd config — device TODO); tailscale takes a key. */
    const char *argv[10] = {0};
    if (!strcmp(type, "wireguard"))
        argv[0] = "pkexec", argv[1] = "wg-quick", argv[2] = "up", argv[3] = p->vpn;
    else if (!strcmp(type, "openvpn"))
        argv[0] = "pkexec", argv[1] = "openvpn", argv[2] = "--config", argv[3] = p->vpn, argv[4] = "--daemon";
    else if (!strcmp(type, "ikev2"))                          /* strongSwan connection */
        argv[0] = "pkexec", argv[1] = "ipsec", argv[2] = "up", argv[3] = p->vpn[0] ? p->vpn : p->vpn_server;
    else if (!strcmp(type, "l2tp"))                           /* xl2tpd control */
        argv[0] = "pkexec", argv[1] = "xl2tpd-control", argv[2] = "connect", argv[3] = p->vpn[0] ? p->vpn : p->vpn_server;
    else if (!strcmp(type, "tailscale")) {
        argv[0] = "pkexec", argv[1] = "tailscale", argv[2] = "up";
        if (p->vpn_secret[0]) argv[3] = "--authkey", argv[4] = p->vpn_secret;
    }
    else
        return -1;

    int rc = run(argv);
    snprintf(s_type, sizeof(s_type), "%s", type);
    snprintf(s_cfg, sizeof(s_cfg), "%s", p->vpn);
    if (rc == 0) { s_we_started = 1; return 0; }
    return -1;   /* caller offers "connect anyway / cancel" */
}

void vpn_down(void)
{
    if (!s_we_started || !s_type[0]) return;
    const char *argv[8] = {0};
    if (!strcmp(s_type, "wireguard"))
        argv[0] = "pkexec", argv[1] = "wg-quick", argv[2] = "down", argv[3] = s_cfg;
    else if (!strcmp(s_type, "openvpn"))
        argv[0] = "pkexec", argv[1] = "pkill", argv[2] = "-f", argv[3] = "openvpn";
    else if (!strcmp(s_type, "ikev2"))
        argv[0] = "pkexec", argv[1] = "ipsec", argv[2] = "down", argv[3] = s_cfg;
    else if (!strcmp(s_type, "l2tp"))
        argv[0] = "pkexec", argv[1] = "xl2tpd-control", argv[2] = "disconnect", argv[3] = s_cfg;
    else if (!strcmp(s_type, "tailscale"))
        argv[0] = "pkexec", argv[1] = "tailscale", argv[2] = "down";
    if (argv[0]) run(argv);
    s_we_started = 0; s_type[0] = 0; s_cfg[0] = 0;
}
