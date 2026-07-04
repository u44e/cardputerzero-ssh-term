/* config.c — connection profiles persistence (flat key=value). */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static profile_t s_prof[CFG_MAX_PROFILES];
static int       s_count = 0;
static macro_t   s_macros[CFG_MAX_MACROS];
static int       s_macro_count = 0;
static int       s_loaded = 0;
static int       s_lang = 0;    /* 0 = en, 1 = ja */

int  config_lang(void) { return s_lang; }
void config_set_lang(int lang) { s_lang = lang ? 1 : 0; }

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
    else if (!strcmp(key, "vpn_type")) snprintf(p->vpn_type, sizeof(p->vpn_type), "%s", val);
    else if (!strcmp(key, "vpn"))   snprintf(p->vpn, sizeof(p->vpn), "%s", val);
    /* legacy vpn_server/user/pass/secret keys are intentionally ignored — secrets
     * are OS-managed now, and dropping them here purges them on the next save. */
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
    s_macro_count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        val[strcspn(val, "\r\n")] = 0;

        if (!strcmp(key, "profiles") || !strcmp(key, "macros")) continue;  /* counts implicit */
        if (!strcmp(key, "lang")) { s_lang = !strcmp(val, "ja"); continue; }
        if (!strncmp(key, "mac", 3) && key[3] >= '0' && key[3] <= '9') {   /* "mac2.name" */
            int mi = atoi(key + 3);
            char *d = strchr(key, '.');
            if (d && mi >= 0 && mi < CFG_MAX_MACROS) {
                if (mi + 1 > s_macro_count) s_macro_count = mi + 1;
                if      (!strcmp(d + 1, "name")) snprintf(s_macros[mi].name, sizeof(s_macros[mi].name), "%s", val);
                else if (!strcmp(d + 1, "text")) snprintf(s_macros[mi].text, sizeof(s_macros[mi].text), "%s", val);
            }
            continue;
        }
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
    fprintf(f, "lang=%s\n", s_lang ? "ja" : "en");
    for (int i = 0; i < s_count; i++) {
        profile_t *p = &s_prof[i];
        fprintf(f, "p%d.name=%s\n",  i, p->name);
        fprintf(f, "p%d.proto=%s\n", i, p->proto);
        fprintf(f, "p%d.host=%s\n",  i, p->host);
        fprintf(f, "p%d.port=%s\n",  i, p->port);
        fprintf(f, "p%d.user=%s\n",  i, p->user);
        fprintf(f, "p%d.vpn_type=%s\n", i, p->vpn_type[0] ? p->vpn_type : "none");
        fprintf(f, "p%d.vpn=%s\n",   i, p->vpn);   /* OS-side connection name; no secrets */
        fprintf(f, "p%d.log=%d\n",   i, p->log);
        fprintf(f, "p%d.size=%s\n",  i, p->size[0] ? p->size : "12");
    }
    fprintf(f, "macros=%d\n", s_macro_count);
    for (int i = 0; i < s_macro_count; i++) {
        fprintf(f, "mac%d.name=%s\n", i, s_macros[i].name);
        fprintf(f, "mac%d.text=%s\n", i, s_macros[i].text);
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

int config_macro_count(void) { return s_macro_count; }

const macro_t *config_macro(int i)
{
    return (i >= 0 && i < s_macro_count) ? &s_macros[i] : NULL;
}

macro_t *config_macro_mutable(int i)
{
    return (i >= 0 && i < s_macro_count) ? &s_macros[i] : NULL;
}

int config_macro_add(void)
{
    if (s_macro_count >= CFG_MAX_MACROS) return -1;
    macro_t *m = &s_macros[s_macro_count];
    memset(m, 0, sizeof(*m));
    snprintf(m->name, sizeof(m->name), "macro");
    return s_macro_count++;
}

void config_macro_delete(int i)
{
    if (i < 0 || i >= s_macro_count) return;
    for (int j = i; j + 1 < s_macro_count; j++) s_macros[j] = s_macros[j + 1];
    s_macro_count--;
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
    if (!strcmp(p->proto, "serial")) {   /* USB-serial console via picocom (host=device, port=baud) */
        snprintf(a0, sizeof(a0), "picocom");
        snprintf(a1, sizeof(a1), "-b");
        snprintf(a2, sizeof(a2), "%s", p->port[0] ? p->port : "115200");
        snprintf(a3, sizeof(a3), "%s", p->host[0] ? p->host : "/dev/ttyUSB0");
        argv[0] = a0; argv[1] = a1; argv[2] = a2; argv[3] = a3; argv[4] = NULL;
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
