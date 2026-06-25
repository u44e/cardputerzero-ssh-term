/* config.c — connection profiles persistence (flat key=value). */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static profile_t s_prof[CFG_MAX_PROFILES];
static int       s_count = 0;
static int       s_loaded = 0;

static const char *conf_path(void)
{
    const char *p = getenv("TERM_CONF");
    return (p && *p) ? p : "/sdcard/term.conf";
}

static void set_field(profile_t *p, const char *key, const char *val)
{
    if      (!strcmp(key, "name"))  snprintf(p->name, sizeof(p->name), "%s", val);
    else if (!strcmp(key, "proto")) snprintf(p->proto, sizeof(p->proto), "%s", val);
    else if (!strcmp(key, "host"))  snprintf(p->host, sizeof(p->host), "%s", val);
    else if (!strcmp(key, "port"))  snprintf(p->port, sizeof(p->port), "%s", val);
    else if (!strcmp(key, "user"))  snprintf(p->user, sizeof(p->user), "%s", val);
    else if (!strcmp(key, "vpn"))   snprintf(p->vpn, sizeof(p->vpn), "%s", val);
    else if (!strcmp(key, "log"))   p->log = atoi(val);
    else if (!strcmp(key, "size"))  snprintf(p->size, sizeof(p->size), "%s", val);
}

static void seed_defaults(void)
{
    const char *user = getenv("USER");
    if (!user || !*user) user = "pi";
    s_count = 0;

    profile_t *p = &s_prof[s_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "local shell");
    snprintf(p->proto, sizeof(p->proto), "shell");

    p = &s_prof[s_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "localhost");
    snprintf(p->proto, sizeof(p->proto), "ssh");
    snprintf(p->host, sizeof(p->host), "localhost");
    snprintf(p->port, sizeof(p->port), "22");
    snprintf(p->user, sizeof(p->user), "%s", user);

    p = &s_prof[s_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "router");
    snprintf(p->proto, sizeof(p->proto), "telnet");
    snprintf(p->host, sizeof(p->host), "192.168.1.1");
    snprintf(p->port, sizeof(p->port), "23");
}

void config_load(void)
{
    if (s_loaded) return;
    s_loaded = 1;

    FILE *f = fopen(conf_path(), "r");
    if (!f) { seed_defaults(); config_save(); return; }

    s_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;

        if (!strcmp(key, "profiles")) continue;     /* count is implicit */
        /* keys look like "p3.host" */
        if (key[0] != 'p') continue;
        int idx = atoi(key + 1);
        char *dot = strchr(key, '.');
        if (!dot || idx < 0 || idx >= CFG_MAX_PROFILES) continue;
        if (idx + 1 > s_count) s_count = idx + 1;
        set_field(&s_prof[idx], dot + 1, val);
    }
    fclose(f);
    if (s_count == 0) { seed_defaults(); config_save(); }
}

int config_save(void)
{
    FILE *f = fopen(conf_path(), "w");
    if (!f) return -1;
    fprintf(f, "profiles=%d\n", s_count);
    for (int i = 0; i < s_count; i++) {
        profile_t *p = &s_prof[i];
        fprintf(f, "p%d.name=%s\n",  i, p->name);
        fprintf(f, "p%d.proto=%s\n", i, p->proto);
        fprintf(f, "p%d.host=%s\n",  i, p->host);
        fprintf(f, "p%d.port=%s\n",  i, p->port);
        fprintf(f, "p%d.user=%s\n",  i, p->user);
        fprintf(f, "p%d.vpn=%s\n",   i, p->vpn);
        fprintf(f, "p%d.log=%d\n",   i, p->log);
        fprintf(f, "p%d.size=%s\n",  i, p->size[0] ? p->size : "12");
    }
    fclose(f);
    return 0;
}

int config_count(void) { return s_count; }

const profile_t *config_get(int i)
{
    if (i < 0 || i >= s_count) return NULL;
    return &s_prof[i];
}

profile_t *config_mutable(int i)
{
    if (i < 0 || i >= s_count) return NULL;
    return &s_prof[i];
}

int config_add(void)
{
    if (s_count >= CFG_MAX_PROFILES) return -1;
    profile_t *p = &s_prof[s_count];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "new");
    snprintf(p->proto, sizeof(p->proto), "ssh");
    snprintf(p->port, sizeof(p->port), "22");
    return s_count++;
}

void config_delete(int i)
{
    if (i < 0 || i >= s_count) return;
    for (int j = i; j + 1 < s_count; j++) s_prof[j] = s_prof[j + 1];
    s_count--;
}

const char *const *config_argv(int i)
{
    static char a0[64], a1[8], a2[16], a3[192];
    static const char *argv[6];
    const profile_t *p = config_get(i);
    if (!p) return NULL;

    if (!strcmp(p->proto, "shell")) {
        const char *sh = getenv("SHELL");
        snprintf(a0, sizeof(a0), "%s", (sh && *sh) ? sh : "/bin/sh");
        argv[0] = a0; argv[1] = NULL;
        return argv;
    }
    if (!strcmp(p->proto, "telnet")) {
        snprintf(a0, sizeof(a0), "telnet");
        snprintf(a3, sizeof(a3), "%s", p->host);
        snprintf(a2, sizeof(a2), "%s", p->port[0] ? p->port : "23");
        argv[0] = a0; argv[1] = a3; argv[2] = a2; argv[3] = NULL;
        return argv;
    }
    /* ssh (default) */
    snprintf(a0, sizeof(a0), "ssh");
    snprintf(a1, sizeof(a1), "-p");
    snprintf(a2, sizeof(a2), "%s", p->port[0] ? p->port : "22");
    if (p->user[0]) snprintf(a3, sizeof(a3), "%s@%s", p->user, p->host);
    else            snprintf(a3, sizeof(a3), "%s", p->host);
    argv[0] = a0; argv[1] = a1; argv[2] = a2; argv[3] = a3; argv[4] = NULL;
    return argv;
}
