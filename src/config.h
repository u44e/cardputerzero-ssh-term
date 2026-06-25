/* config.h — connection profiles, flat key=value persistence.
 * File: $TERM_CONF else /sdcard/term.conf. Seeds defaults if missing. */
#ifndef SSH_TERM_CONFIG_H
#define SSH_TERM_CONFIG_H

#define CFG_MAX_PROFILES 16

typedef struct {
    char name[32];
    char proto[8];   /* "ssh" | "telnet" | "shell" */
    char host[128];
    char port[8];
    char user[64];
    char vpn_type[12]; /* none|wireguard|openvpn|ikev2|l2tp|tailscale */
    char vpn[96];    /* config name/path (wg/ovpn) or remote ID (ikev2) */
    char vpn_server[80];  /* server host (ikev2/l2tp) */
    char vpn_user[48];    /* username / account (openvpn/ikev2/l2tp) */
    char vpn_pass[64];    /* password */
    char vpn_secret[96];  /* PSK / shared secret (l2tp) or auth key (tailscale) */
    int  log;        /* 1 = save session log */
    char size[4];    /* terminal font px: "12" | "16" | "20" */
} profile_t;

void              config_load(void);          /* load (or seed defaults) */
int               config_save(void);          /* 0 ok, -1 fail */
int               config_lang(void);          /* 0 = en, 1 = ja */
void              config_set_lang(int lang);  /* persists on next save */
int               config_count(void);
const profile_t  *config_get(int i);          /* NULL if out of range */
profile_t        *config_mutable(int i);      /* for the editor */
int               config_add(void);           /* new blank profile -> index, or -1 */
void              config_delete(int i);

/* Build an argv for connecting profile i. Fills argv[] (NULL-terminated) using
 * the static storage in *buf. Returns argv (do not free); NULL on bad proto. */
const char *const *config_argv(int i);

#endif /* SSH_TERM_CONFIG_H */
