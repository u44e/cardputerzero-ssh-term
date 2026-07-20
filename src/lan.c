/* lan.c — wired-LAN gate (direct-cable sessions with a per-profile static IP).
 *
 * Lifecycle: lan_boot_down() at app start disables every interface a profile
 * names, so plugging a cable in cannot DHCP / conflict on a customer network.
 * lan_up() at connect time gives the port the profile's address and raises the
 * link; lan_session_down() returns to the disabled baseline when the session
 * ends. lan_restore() at app exit hands the device back to NetworkManager,
 * which re-applies its own persistent config — the original state (DHCP or
 * static) is restored by construction, with nothing to back up or parse.
 *
 * Exec is device-only (root via pkexec/polkit, same as vpn.c) and always
 * fork/exec with fixed argv — never a shell, so profile strings can't inject. */
#include "lan.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define LAN_DEF_IF   "eth0"
#define LAN_MAX_IFS  4        /* distinct interfaces named across profiles */

static char s_boot_if[LAN_MAX_IFS][sizeof(((profile_t *)0)->lan_if)];
static int  s_boot_n;         /* interfaces we unmanaged/downed at app start */
static char s_cfg_if[sizeof(((profile_t *)0)->lan_if)];   /* holding our IP now */

static int nm_present(void)
{
    return access("/usr/bin/nmcli", X_OK) == 0 || access("/bin/nmcli", X_OK) == 0;
}

/* run argv synchronously; returns child exit code, or -1 (same as vpn.c) */
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

/* Profile strings become argv words for pkexec'd tools — accept only the
 * shapes those tools expect so nothing can ever look like an option. */
static int valid_addr(const char *a)   /* IPv4/prefix: digits, dots, one '/' */
{
    int dots = 0, slash = 0;
    if (!a || a[0] < '0' || a[0] > '9') return 0;
    for (const char *c = a; *c; c++) {
        if (*c == '.') dots++;
        else if (*c == '/') slash++;
        else if (*c < '0' || *c > '9') return 0;
    }
    return dots == 3 && slash <= 1;
}

static int valid_if(const char *n)     /* eth0, enx…, usb0 — alnum plus -_. */
{
    if (!n || !*n || n[0] == '-') return 0;
    for (const char *c = n; *c; c++)
        if (!(((*c | 0x20) >= 'a' && (*c | 0x20) <= 'z') ||
              (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.'))
            return 0;
    return 1;
}

static const char *prof_if(const profile_t *p)
{
    return p->lan_if[0] ? p->lan_if : LAN_DEF_IF;
}

static int prof_uses_lan(const profile_t *p)
{
    return p && p->lan[0] &&
           (!strcmp(p->proto, "ssh") || !strcmp(p->proto, "telnet"));
}

static void if_disable(const char *ifname)   /* unmanage first, then link down */
{
    if (nm_present()) {
        const char *nm[] = { "pkexec", "nmcli", "device", "set", ifname, "managed", "no", NULL };
        run(nm);
    }
    const char *dn[] = { "pkexec", "ip", "link", "set", ifname, "down", NULL };
    run(dn);
}

void lan_boot_down(void)
{
    s_boot_n = 0;
    s_cfg_if[0] = 0;
    for (int i = 0; i < config_count(); i++) {
        const profile_t *p = config_get(i);
        if (!prof_uses_lan(p) || !valid_if(prof_if(p))) continue;
        const char *ifname = prof_if(p);
        int seen = 0;
        for (int j = 0; j < s_boot_n; j++)
            if (!strcmp(s_boot_if[j], ifname)) { seen = 1; break; }
        if (seen || s_boot_n >= LAN_MAX_IFS) continue;
        snprintf(s_boot_if[s_boot_n++], sizeof(s_boot_if[0]), "%s", ifname);
        if_disable(ifname);
    }
}

int lan_up(const profile_t *p)
{
    if (!prof_uses_lan(p)) return 0;
    const char *ifname = prof_if(p);
    if (!valid_if(ifname) || !valid_addr(p->lan)) return -1;

    if (nm_present()) {   /* keep NM's hands off even if boot-down never ran */
        const char *nm[] = { "pkexec", "nmcli", "device", "set", ifname, "managed", "no", NULL };
        run(nm);
    }
    /* bare "a.b.c.d" would become a /32 (no on-link route) — assume /24 */
    char addr[24];
    snprintf(addr, sizeof(addr), "%s%s", p->lan, strchr(p->lan, '/') ? "" : "/24");

    const char *fl[] = { "pkexec", "ip", "addr", "flush", "dev", ifname, NULL };
    run(fl);
    const char *ad[] = { "pkexec", "ip", "addr", "add", addr, "dev", ifname, NULL };
    if (run(ad) != 0) return -1;
    const char *up[] = { "pkexec", "ip", "link", "set", ifname, "up", NULL };
    if (run(up) != 0) return -1;
    snprintf(s_cfg_if, sizeof(s_cfg_if), "%s", ifname);

    /* brief carrier wait so the first connect doesn't race link negotiation;
     * no carrier is not fatal (the peer may still be booting) */
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", ifname);
    for (int t = 0; t < 30; t++) {                     /* <= 3 s */
        FILE *f = fopen(path, "r");
        if (!f) break;                                 /* no sysfs (emu) — skip */
        int c = fgetc(f);
        fclose(f);
        if (c == '1') break;
        usleep(100 * 1000);
    }
    return 0;
}

void lan_session_down(void)
{
    if (!s_cfg_if[0]) return;
    const char *fl[] = { "pkexec", "ip", "addr", "flush", "dev", s_cfg_if, NULL };
    run(fl);
    const char *dn[] = { "pkexec", "ip", "link", "set", s_cfg_if, "down", NULL };
    run(dn);
    s_cfg_if[0] = 0;
}

void lan_restore(void)
{
    lan_session_down();
    for (int i = 0; i < s_boot_n; i++) {
        if (nm_present()) {   /* NM re-applies its own config: the pre-app state */
            const char *nm[] = { "pkexec", "nmcli", "device", "set", s_boot_if[i], "managed", "yes", NULL };
            run(nm);
        } else {
            const char *up[] = { "pkexec", "ip", "link", "set", s_boot_if[i], "up", NULL };
            run(up);
        }
    }
    s_boot_n = 0;
}
