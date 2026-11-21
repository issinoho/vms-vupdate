/* vupdate.c - the VUPDATE verb (vupdate.cld): LOGIN, LOGOUT, CHECK (phase 1).
 * SHOW and UPGRADE come in phase 2.
 *
 * Files (logicals override for development, see dev_setup.com):
 *   credentials  VUPDATE$CREDENTIALS, else VUPDATE$ROOT:[DATA]CREDENTIALS.DAT
 *   CA bundle    VUPDATE$CA_FILE,     else VUPDATE$ROOT:[DATA]CACERT.PEM
 *   cache        VUPDATE$ROOT:[CACHE]  catalogue 1 h, package details 24 h
 *   proxy        VUPDATE$PROXY (a URL), when defined */
#include "vu_auth.h"
#include "vu_cat.h"
#include "vu_http.h"
#include "vu_kit.h"
#include "vu_pcsi.h"
#include "vu_plan.h"
#include "vu_portal.h"
#include "vu_report.h"
#include "vu_store.h"
#include "vu_ver.h"

#include <climsgdef.h>
#include <clidef.h>
#include <cli$routines.h>
#include <descrip.h>
#include <fcntl.h>
#include <iodef.h>
#include <hlpdef.h>
#include <lbr$routines.h>
#include <ctype.h>
#include <lib$routines.h>
#include <ssdef.h>
#include <starlet.h>
#include <sys/stat.h>
#include <stsdef.h>
#include <syidef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* POSIX, hidden by stdio.h under /STANDARD=C99 (DECC$FDOPEN via ALL_ENTRIES). */
FILE *fdopen(int fd, const char *mode);

/* Condition values of vupdate_msg.msg: customer facility 1903 (bit 27), facility-
 * specific (bit 15), messages numbered from 1. MESSAGE/LIST shows the values
 * (CURRENT %X0F6F8009, FAILED %X0F6F807A). */
#define VU_MSG(n, sev) \
    (0x08000000u | (1903u << 16) | 0x8000u | ((unsigned int)(n) << 3) | (sev))
#define VU__CURRENT VU_MSG(1, STS$K_SUCCESS)
#define VU__LOGGEDIN VU_MSG(2, STS$K_SUCCESS)
#define VU__LOGGEDOUT VU_MSG(3, STS$K_SUCCESS)
#define VU__UPDATES VU_MSG(4, STS$K_INFO)
#define VU__NOTSAVED VU_MSG(5, STS$K_INFO)
#define VU__NYI VU_MSG(6, STS$K_WARNING)
#define VU__NOCRED VU_MSG(7, STS$K_ERROR)
#define VU__AUTHFAIL VU_MSG(8, STS$K_ERROR)
#define VU__NETERR VU_MSG(9, STS$K_ERROR)
#define VU__PORTAL VU_MSG(10, STS$K_ERROR)
#define VU__BADREPLY VU_MSG(11, STS$K_ERROR)
#define VU__STORE VU_MSG(12, STS$K_ERROR)
#define VU__PCSI VU_MSG(13, STS$K_ERROR)
#define VU__NOTERM VU_MSG(14, STS$K_ERROR)
#define VU__FAILED VU_MSG(15, STS$K_ERROR)
#define VU__DOWNLOADED VU_MSG(16, STS$K_SUCCESS)
#define VU__INSTALLED VU_MSG(17, STS$K_SUCCESS)
#define VU__NOTHING VU_MSG(18, STS$K_INFO)
#define VU__NOTFOUND VU_MSG(19, STS$K_WARNING)
#define VU__REBOOT VU_MSG(20, STS$K_WARNING)
#define VU__MD5 VU_MSG(21, STS$K_ERROR)
#define VU__KIT VU_MSG(22, STS$K_ERROR)
#define VU__ABORTED VU_MSG(23, STS$K_ERROR)

#define CAT_MAX_AGE 3600
#define DETAIL_MAX_AGE 86400

/* ---------- small helpers ---------- */

static void dsc(struct dsc$descriptor_s *d, const char *s, size_t len)
{
    d->dsc$w_length = (unsigned short)len;
    d->dsc$b_dtype = DSC$K_DTYPE_T;
    d->dsc$b_class = DSC$K_CLASS_S;
    d->dsc$a_pointer = (char *)s;
}

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

/* Print a message (and return its value with the inhibit bit, for exit). */
static unsigned int msg(unsigned int code, int nfao, const char *a, const char *b)
{
    unsigned int v[6];
    v[0] = 2 + (unsigned int)nfao;
    v[1] = code;
    v[2] = (unsigned int)nfao;
    v[3] = (unsigned int)(size_t)a;
    v[4] = (unsigned int)(size_t)b;
    fflush(stdout);
    sys$putmsg(v, 0, 0, 0);
    return code | STS$M_INHIB_MSG;
}

static unsigned int msg_n(unsigned int code, unsigned int n)
{
    unsigned int v[4];
    v[0] = 3;
    v[1] = code;
    v[2] = 1;
    v[3] = n;
    fflush(stdout);
    sys$putmsg(v, 0, 0, 0);
    return code | STS$M_INHIB_MSG;
}

#define VU_E_CRED -6 /* credentials missing or unusable: message already shown */
static unsigned int cred_status;

/* A message with a number then a string (!UL, !AZ). */
static unsigned int msg_ns(unsigned int code, unsigned int n, const char *a)
{
    unsigned int v[5];
    v[0] = 4;
    v[1] = code;
    v[2] = 2;
    v[3] = n;
    v[4] = (unsigned int)(size_t)a;
    fflush(stdout);
    sys$putmsg(v, 0, 0, 0);
    return code | STS$M_INHIB_MSG;
}

static unsigned int portal_msg(int rc, const char *err)
{
    switch (rc) {
    case VU_E_CRED: return cred_status;
    case VU_E_AUTH: return msg(VU__AUTHFAIL, 1, err, 0);
    case VU_E_NET: return msg(VU__NETERR, 1, err, 0);
    case VU_E_PORTAL: return msg(VU__PORTAL, 1, err, 0);
    case VU_E_REPLY: return msg(VU__BADREPLY, 1, err, 0);
    default: return msg(VU__FAILED, 1, err[0] ? err : "out of memory", 0);
    }
}

static const char *path_of(const char *logical, const char *dflt)
{
    const char *v = getenv(logical);
    return v && *v ? v : dflt;
}

static int present(const char *entity)
{
    struct dsc$descriptor_s d;
    unsigned int st;
    dsc(&d, entity, strlen(entity));
    st = cli$present(&d);
    return st == CLI$_PRESENT || st == CLI$_DEFAULTED;
}

static int value(const char *entity, char *buf, size_t sz)
{
    struct dsc$descriptor_s d, r;
    unsigned short len = 0;
    dsc(&d, entity, strlen(entity));
    dsc(&r, buf, sz - 1);
    if (!(cli$get_value(&d, &r, &len) & 1)) {
        buf[0] = '\0';
        return 0;
    }
    buf[len] = '\0';
    return 1;
}

static void syi_str(int item, char *buf, size_t sz)
{
    struct dsc$descriptor_s r;
    unsigned short len = 0;
    int code = item;
    dsc(&r, buf, sz - 1);
    if (!(lib$getsyi(&code, 0, &r, &len, 0, 0) & 1))
        len = 0;
    buf[len] = '\0';
    rtrim(buf);
}

/* Read a whole file (text) into malloc'd memory; NULL when missing. */
static char *slurp(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    size_t n = 0, cap = 65536, r;
    char *buf;
    if (!f)
        return NULL;
    buf = (char *)malloc(cap + 1);
    while (buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            char *nb = cap * 2 > max ? NULL : (char *)realloc(buf, cap * 2 + 1);
            if (!nb) {
                free(buf);
                buf = NULL;
                break;
            }
            buf = nb;
            cap *= 2;
        }
    }
    fclose(f);
    if (buf)
        buf[n] = '\0';
    return buf;
}

static int write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    size_t n = strlen(text);
    if (!f)
        return -1;
    if (fwrite(text, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f) ? -1 : 0;
}

/* Delete every version below the newest ("file;-1" names the next lower one). */
static void purge(const char *path)
{
    char spec[300];
    snprintf(spec, sizeof spec, "%s;-1", path);
    while (remove(spec) == 0)
        ;
}

static long age_of(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    return (long)(time(NULL) - st.st_mtime);
}

/* ---------- terminal input ---------- */

/* Prompt on SYS$COMMAND; echo off for passwords. Returns 0 when there is no
 * terminal (batch) or the read failed or was ended with Ctrl/Z. */
static int ask(const char *prompt, int echo, char *out, size_t sz)
{
    struct dsc$descriptor_s d;
    unsigned short chan, iosb[4];
    unsigned int st, fn = IO$_READPROMPT | (echo ? 0 : IO$M_NOECHO);
    int ok = 0;
    out[0] = '\0';
    if (!isatty(0))
        return 0;
    dsc(&d, "SYS$COMMAND", 11);
    fflush(stdout);
    if (!(sys$assign(&d, &chan, 0, 0, 0) & 1))
        return 0;
    st = sys$qiow(0, chan, fn, (void *)iosb, 0, 0, out, (unsigned int)sz - 1, 0, 0,
                  (void *)prompt, (unsigned int)strlen(prompt));
    if ((st & 1) && (iosb[0] & 1)) {
        out[iosb[1]] = '\0';
        ok = 1;
    }
    sys$dassgn(chan);
    return ok;
}

static int http_start(void)
{
    return vu_http_init(path_of("VUPDATE$CA_FILE", "VUPDATE$ROOT:[DATA]CACERT.PEM"),
                        getenv("VUPDATE$PROXY"));
}

/* ---------- credentials ---------- */

/* Node identity the store is sealed with (vu_store.h explains what this buys). */
static void node_key(char *buf, size_t sz)
{
    char node[32], sysid[32], hw[128];
    syi_str(SYI$_SCSNODE, node, sizeof node);
    syi_str(SYI$_SCSSYSTEMID, sysid, sizeof sysid);
    syi_str(SYI$_HW_NAME, hw, sizeof hw);
    snprintf(buf, sz, "%s|%s|%s", node, sysid, hw);
}

static const char *store_path(void)
{
    return path_of("VUPDATE$CREDENTIALS", "VUPDATE$ROOT:[DATA]CREDENTIALS.DAT");
}

/* Load the stored credentials into a session. Returns 0 or a message value. */
static unsigned int load_session(vu_session *s)
{
    char key[256], user[VU_USER_MAX], pw[VU_PASSWORD_MAX];
    char *text = slurp(store_path(), VU_STORE_TEXT_MAX * 4);
    int rc;
    if (!text)
        return msg(VU__NOCRED, 1, store_path(), 0);
    node_key(key, sizeof key);
    rc = vu_store_open(key, text, user, sizeof user, pw, sizeof pw);
    vu_wipe(text, strlen(text));
    free(text);
    vu_wipe(key, sizeof key);
    if (rc == -2)
        return msg(VU__STORE, 2, store_path(),
                   "does not open on this node (copied, SCSNODE changed, or modified): "
                   "use VUPDATE LOGIN again");
    if (rc != 0)
        return msg(VU__STORE, 2, store_path(), "is not a VUPDATE credential file");
    vu_session_init(s, user, pw);
    vu_wipe(pw, sizeof pw);
    return 0;
}

static unsigned int do_login(void)
{
    char user[VU_USER_MAX], pw[VU_PASSWORD_MAX], key[256], text[VU_STORE_TEXT_MAX];
    vu_session s;
    char *who = NULL;
    int rc, fd;
    size_t n;
    if (!value("USERNAME", user, sizeof user)) {
        if (!ask("VSI portal user (e-mail): ", 1, user, sizeof user))
            return msg(VU__NOTERM, 1, "VUPDATE LOGIN", 0);
    }
    rtrim(user);
    if (!user[0])
        return msg(VU__AUTHFAIL, 1, "no user name given", 0);
    if (!ask("Password: ", 0, pw, sizeof pw))
        return msg(VU__NOTERM, 1, "VUPDATE LOGIN", 0);
    vu_session_init(&s, user, pw);
    vu_wipe(pw, sizeof pw);
    if (http_start() != 0) {
        vu_session_end(&s);
        return msg(VU__FAILED, 1, "cannot initialise libcurl", 0);
    }
    /* Prove the credentials before saving them: a token, then /vsp/whoami. */
    rc = vu_portal_signin(&s);
    if (rc == VU_OK)
        rc = vu_portal_whoami(&s, &who);
    free(who);
    vu_http_cleanup();
    if (rc != VU_OK) {
        unsigned int st = portal_msg(rc, s.err);
        vu_session_end(&s);
        return st;
    }
    node_key(key, sizeof key);
    rc = vu_store_seal(key, s.user, s.password, text, sizeof text);
    vu_wipe(key, sizeof key);
    vu_session_end(&s);
    if (rc != 0)
        return msg(VU__STORE, 2, store_path(), "cannot seal the credentials");
    /* Owner read/write only (S:RWED and O:RWED, G and W none). */
    fd = open(store_path(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    n = strlen(text);
    if (fd < 0 || write(fd, text, n) != (ssize_t)n || close(fd) != 0) {
        vu_wipe(text, sizeof text);
        if (fd >= 0)
            remove(store_path());
        return msg(VU__STORE, 2, store_path(), "cannot be written");
    }
    vu_wipe(text, sizeof text);
    purge(store_path());
    return msg(VU__LOGGEDIN, 2, user, store_path());
}

static unsigned int do_logout(void)
{
    int n = 0;
    while (remove(store_path()) == 0)
        n++;
    if (!n)
        return msg(VU__NOTSAVED, 1, store_path(), 0);
    return msg(VU__LOGGEDOUT, 1, store_path(), 0);
}

/* ---------- CHECK ---------- */

/* PRODUCT SHOW PRODUCT * /FULL in a subprocess, its output in a scratch file. */
static unsigned int inventory(vu_inventory *inv)
{
    static const char cmd[] = "PRODUCT SHOW PRODUCT * /FULL";
    char out[64], why[64];
    struct dsc$descriptor_s dc, di, dout;
    unsigned int st, status = 0, flags = CLI$M_NOCLISYM | CLI$M_NOKEYPAD;
    char *text;
    int rc;
    snprintf(out, sizeof out, "SYS$SCRATCH:VUPDATE_%X.LIS", (unsigned int)getpid());
    dsc(&dc, cmd, strlen(cmd));
    dsc(&di, "NL:", 3);
    dsc(&dout, out, strlen(out));
    /* NOCLISYM: a user's PRODUCT symbol must not run instead. */
    st = lib$spawn(&dc, &di, &dout, &flags, 0, 0, &status, 0, 0, 0, 0, 0, 0);
    text = slurp(out, 8L << 20);
    while (remove(out) == 0)
        ;
    if (!(st & 1) || !(status & 1)) {
        free(text);
        snprintf(why, sizeof why, "PRODUCT SHOW PRODUCT failed (%%X%08X)",
                 (st & 1) ? status : st);
        return msg(VU__PCSI, 1, why, 0);
    }
    if (!text)
        return msg(VU__PCSI, 1, "no output from PRODUCT SHOW PRODUCT", 0);
    rc = vu_pcsi_parse(text, inv);
    free(text);
    if (rc != 0)
        return msg(VU__PCSI, 1, rc == -2 ? "out of memory" : "no product table in the output", 0);
    return 0;
}

/* Fetch a reply through the cache: path is used when younger than max_age (and
 * refresh is 0); otherwise fetched and written back. *from_cache says which. */
typedef int (*fetch_fn)(vu_session *, const void *, char **);

static int fetch_cat(vu_session *s, const void *arch, char **json)
{
    return vu_portal_catalog(s, (const char *)arch, json);
}

static int fetch_det(vu_session *s, const void *num, char **json)
{
    return vu_portal_detail(s, *(const long *)num, json);
}

/* The credentials are loaded on the first fetch, so a CHECK answered from the
 * cache needs neither them nor the network. When loading fails, cached() returns
 * VU_E_CRED and cred_status holds the message value already shown. */
static int cached(vu_session *s, const char *path, long max_age, int refresh, fetch_fn fn,
                  const void *arg, char **json, long *age)
{
    int rc;
    *age = refresh ? -1 : age_of(path);
    if (*age >= 0 && *age < max_age && (*json = slurp(path, 32L << 20)) != NULL)
        return VU_OK;
    *age = -1;
    *json = NULL;
    if (!s->user[0] && (cred_status = load_session(s)) != 0)
        return VU_E_CRED;
    rc = fn(s, arg, json);
    if (rc == VU_OK) {
        if (write_file(path, *json) != 0)
            remove(path); /* never leave a partial cache file */
        else
            purge(path);
    }
    return rc;
}

/* ---------- shared: inventory, catalogue and plan ---------- */

typedef struct {
    vu_session s;
    vu_inventory inv;
    vu_catalog cat;
    vu_plan pl;
    int *flags; /* VU_FL_* per finding */
    char arch_name[32], node[32], os[32];
    const char *arch;
    int archbit, opts, refresh;
    long cat_age;
    int have_inv, have_cat, have_plan, http;
} ctx_t;

static void ctx_close(ctx_t *c)
{
    free(c->flags);
    if (c->have_plan)
        vu_plan_free(&c->pl);
    if (c->have_cat)
        vu_catalog_free(&c->cat);
    if (c->have_inv)
        vu_inventory_free(&c->inv);
    if (c->http)
        vu_http_cleanup();
    vu_session_end(&c->s);
    memset(c, 0, sizeof *c);
}

static int same_name(const char *a, const char *b)
{
    while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* A package detail through the 24 h cache. Returns 0 or a message value. */
static unsigned int get_detail(ctx_t *c, long num, vu_detail *d)
{
    char dpath[96], err[512];
    char *json = NULL;
    long age;
    int rc;
    snprintf(dpath, sizeof dpath, "VUPDATE$ROOT:[CACHE]DETAIL_%ld.JSON", num);
    rc = cached(&c->s, dpath, DETAIL_MAX_AGE, c->refresh, fetch_det, &num, &json, &age);
    if (rc != VU_OK)
        return portal_msg(rc, c->s.err);
    rc = vu_detail_parse(json, d, err, sizeof err);
    free(json);
    if (rc != 0) {
        remove(dpath);
        return rc == -3 ? msg(VU__PORTAL, 1, err, 0) : msg(VU__BADREPLY, 1, "package detail", 0);
    }
    return 0;
}

/* Inventory, catalogue (minos merged, D7), plan and flags. 0 or a message value. */
static unsigned int ctx_open(ctx_t *c)
{
    char cpath[96], err[512];
    const vu_product *osp;
    char *json = NULL;
    unsigned int st;
    size_t i;
    int rc;

    syi_str(SYI$_ARCH_NAME, c->arch_name, sizeof c->arch_name);
    syi_str(SYI$_SCSNODE, c->node, sizeof c->node);
    c->archbit = vu_arch_from_name(c->arch_name);
    c->arch = vu_arch_portal_name(c->archbit);
    if (c->archbit != VU_ARCH_X86 && c->archbit != VU_ARCH_AXP)
        return msg(VU__FAILED, 1, "VUPDATE supports x86-64 and Alpha only", 0);
    if ((st = inventory(&c->inv)) != 0)
        return st;
    c->have_inv = 1;
    /* The running version as PCSI names it (V8.4-2L2, E9.2-4): both have the
     * platform product OPENVMS; Alpha also the OS product VMS. */
    osp = vu_inventory_find(&c->inv, "OPENVMS");
    if (!osp || !osp->ver[0])
        osp = vu_inventory_find(&c->inv, "VMS");
    if (osp && osp->ver[0])
        strcpy(c->os, osp->ver);
    else
        syi_str(SYI$_VERSION, c->os, sizeof c->os);

    if (http_start() != 0)
        return msg(VU__FAILED, 1, "cannot initialise libcurl", 0);
    c->http = 1;
    snprintf(cpath, sizeof cpath, "VUPDATE$ROOT:[CACHE]CATALOG_%s.JSON", c->arch);
    rc = cached(&c->s, cpath, CAT_MAX_AGE, c->refresh, fetch_cat, c->arch, &json, &c->cat_age);
    if (rc != VU_OK)
        return portal_msg(rc, c->s.err);
    rc = vu_catalog_parse(json, &c->cat, err, sizeof err);
    free(json);
    if (rc != 0) {
        remove(cpath);
        return rc == -3 ? msg(VU__PORTAL, 1, err, 0) : msg(VU__BADREPLY, 1, "catalogue", 0);
    }
    c->have_cat = 1;

    /* Catalogue rows carry only the first minos of a patch (D7). */
    for (i = 0; i < c->cat.n; i++) {
        static vu_detail d;
        if (!vu_patch_needs_detail(&c->cat.e[i], c->os))
            continue;
        if ((st = get_detail(c, c->cat.e[i].package_number, &d)) != 0)
            return st;
        vu_entry_set_minos(&c->cat.e[i], &d);
    }

    if (vu_plan_build(&c->inv, &c->cat, c->os, c->archbit, c->opts, &c->pl) != 0)
        return msg(VU__FAILED, 1, "out of memory", 0);
    c->have_plan = 1;
    c->flags = (int *)calloc(c->pl.n ? c->pl.n : 1, sizeof *c->flags);
    if (!c->flags)
        return msg(VU__FAILED, 1, "out of memory", 0);
    /* Patches: flags from their detail; updates: from the catalogue text. */
    for (i = 0; i < c->pl.n; i++) {
        const vu_finding *f = &c->pl.f[i];
        static vu_detail d;
        if (!f->entry || !vu_finding_actionable(f->kind))
            continue;
        if (f->kind == VU_F_PATCH) {
            if ((st = get_detail(c, f->entry->package_number, &d)) != 0)
                return st;
            c->flags[i] = VU_FL_DETAIL | (d.security ? VU_FL_SECURITY : 0)
                          | (d.restart_required == 1 ? VU_FL_REBOOT : 0);
        } else if (vu_text_is_security(f->entry->description)) {
            c->flags[i] = VU_FL_SECURITY;
        }
    }
    return 0;
}

static void ctx_options(ctx_t *c)
{
    memset(c, 0, sizeof *c); /* the session signs in on the first portal request */
    c->refresh = present("REFRESH");
    if (present("FIELD_TEST"))
        c->opts |= VU_OPT_FIELD_TEST;
}

/* ---------- CHECK ---------- */

static unsigned int do_check(void)
{
    ctx_t c;
    char line[160], outspec[256], when[32];
    int only_sec = present("SECURITY"), show_all = present("ALL");
    unsigned int st;
    size_t i, k, nact = 0, nvsi = 0;
    FILE *out = stdout;
    time_t now = time(NULL);
    /* Report order (vu_plan's sort order), not the enum's. */
    static const vu_find_t order[] = {VU_F_OSUPGRADE, VU_F_PATCH, VU_F_UPDATE, VU_F_CURRENT,
                                      VU_F_NOTINCAT};

    ctx_options(&c);
    if ((st = ctx_open(&c)) != 0) {
        ctx_close(&c);
        return st;
    }
    if (value("OUTPUT", outspec, sizeof outspec) && !(out = fopen(outspec, "w"))) {
        ctx_close(&c);
        return msg(VU__FAILED, 1, "cannot create the /OUTPUT file", 0);
    }
    strftime(when, sizeof when, "%d-%b-%Y %H:%M", localtime(&now));
    fprintf(out, "VUPDATE CHECK on %s: OpenVMS %s %s, %s\n", c.node, c.os, c.arch_name, when);
    if (c.cat_age >= 0)
        fprintf(out, "VSI catalogue: %lu kits (cached %ld min ago; /REFRESH to fetch now)\n",
                (unsigned long)c.cat.n, c.cat_age / 60);
    else
        fprintf(out, "VSI catalogue: %lu kits (fetched now)\n", (unsigned long)c.cat.n);
    if (only_sec)
        fprintf(out, "Showing security fixes only (/SECURITY).\n");
    for (k = 0; k < sizeof order / sizeof order[0]; k++) {
        int head = 0;
        for (i = 0; i < c.pl.n; i++) {
            const vu_finding *f = &c.pl.f[i];
            if (f->kind != order[k] || (!vu_finding_actionable(f->kind) && !show_all))
                continue;
            if (only_sec && !(c.flags[i] & VU_FL_SECURITY))
                continue;
            if (!head) {
                fprintf(out, "\n%s\n", vu_report_heading(f->kind));
                head = 1;
            }
            vu_report_line(f, c.flags[i], line, sizeof line);
            fprintf(out, "%s\n", line);
        }
    }
    for (i = 0; i < c.pl.n; i++) {
        if (c.pl.f[i].installed && c.pl.f[i].kind != VU_F_PATCH)
            nvsi++;
        if (vu_finding_actionable(c.pl.f[i].kind)
            && (!only_sec || (c.flags[i] & VU_FL_SECURITY)))
            nact++;
    }
    if (!show_all)
        fprintf(out, "\n(/ALL also lists the products that are up to date or not in the "
                     "catalogue.)\n");
    if (out != stdout)
        fclose(out);
    ctx_close(&c);
    return nact ? msg_n(VU__UPDATES, (unsigned int)nact) : msg_n(VU__CURRENT, (unsigned int)nvsi);
}

/* ---------- SHOW ---------- */

static int prereq_applies(const ctx_t *c, const vu_prereq *q);

static const char *type_name(char t)
{
    switch (t) {
    case VU_T_PATCH: return "patch (ECO)";
    case VU_T_LP: return "layered product";
    case VU_T_OPEN: return "open source";
    case VU_T_OS: return "OpenVMS";
    case VU_T_LICENCE: return "licence bundle";
    case VU_T_WINDOWS: return "Windows tool";
    default: return "?";
    }
}

/* Print text wrapped at 76 columns with an indent, whitespace collapsed. */
static void wrap(const char *t, const char *indent)
{
    size_t col = 0, il = strlen(indent);
    while (*t) {
        size_t wl = 0;
        while (*t && isspace((unsigned char)*t))
            t++;
        while (t[wl] && !isspace((unsigned char)t[wl]))
            wl++;
        if (!wl)
            break;
        if (col && col + 1 + wl > 76) {
            printf("\n");
            col = 0;
        }
        if (!col) {
            printf("%s", indent);
            col = il;
        } else {
            printf(" ");
            col++;
        }
        printf("%.*s", (int)wl, t);
        col += wl;
        t += wl;
    }
    if (col)
        printf("\n");
}

static void mb(unsigned long bytes, char *buf, size_t sz)
{
    snprintf(buf, sz, "%lu.%lu MB", bytes / 1048576UL, (bytes % 1048576UL) * 10 / 1048576UL);
}

static unsigned int do_show(void)
{
    ctx_t c;
    char name[64], size[32];
    const vu_entry *target = NULL;
    const vu_product *have;
    static vu_detail d;
    unsigned int st;
    size_t i;
    int j, others = 0;

    value("PRODUCT", name, sizeof name);
    ctx_options(&c);
    if ((st = ctx_open(&c)) != 0) {
        ctx_close(&c);
        return st;
    }
    /* The planned target, else the newest applicable kit, else the newest kit. */
    for (i = 0; i < c.pl.n; i++)
        if (c.pl.f[i].entry && same_name(c.pl.f[i].entry->product, name))
            target = c.pl.f[i].entry;
    for (i = 0; i < c.cat.n && !target; i++) {
        const vu_entry *e = &c.cat.e[i];
        size_t k;
        if (!same_name(e->product, name) || !vu_entry_applies(e, c.os, c.archbit))
            continue;
        for (k = 0; k < c.cat.n; k++)
            if (same_name(c.cat.e[k].product, name) && vu_entry_applies(&c.cat.e[k], c.os, c.archbit)
                && vu_ver_cmp(c.cat.e[k].ver, e->ver) > 0)
                break;
        if (k == c.cat.n)
            target = e;
    }
    if (!target) {
        const vu_entry *best = NULL;
        for (i = 0; i < c.cat.n; i++)
            if (same_name(c.cat.e[i].product, name)
                && (!best || vu_ver_cmp(c.cat.e[i].ver, best->ver) > 0))
                best = &c.cat.e[i];
        target = best;
    }
    if (!target) {
        ctx_close(&c);
        return msg(VU__NOTFOUND, 2, name, "not in the VSI catalogue for this architecture");
    }
    if ((st = get_detail(&c, target->package_number, &d)) != 0) {
        ctx_close(&c);
        return st;
    }
    have = vu_inventory_find(&c.inv, target->product);
    printf("%s (%s)\n", target->kitname, type_name(target->type));
    printf("  Installed:        %s\n", have ? have->ver : "(not installed)");
    printf("  This kit:         %s, released %s%s\n", target->ver,
           target->release_date[0] ? target->release_date : "?",
           vu_ver_is_release(target->ver) ? "" : " (field test)");
    printf("  For this node:    %s (OpenVMS %s)\n",
           vu_entry_applies(target, c.os, c.archbit) ? "yes" : "NO", c.os);
    printf("  OpenVMS versions:");
    for (j = 0; j < d.entry.nminos; j++)
        printf("%s %s", j ? "," : "", d.entry.minos[j]);
    printf("%s\n", d.entry.nminos ? "" : " (any)");
    printf("  Restart required: %s\n",
           d.restart_required == 1 ? "yes" : d.restart_required == 0 ? "no" : "not given");
    printf("  Security fix:     %s\n", d.security ? "yes (by its description)" : "not stated");
    for (j = 0; j < d.nfiles; j++) {
        mb((unsigned long)d.files[j].size, size, sizeof size);
        printf("  File:             %s, %s, MD5 %s\n", d.files[j].save_as, size,
               d.files[j].md5[0] ? d.files[j].md5 : "(none given)");
    }
    for (j = 0; j < d.nprereq; j++)
        printf("  Needs first:      %s%s\n", d.prereq[j].kitname,
               !prereq_applies(&c, &d.prereq[j])               ? " (for another OpenVMS version)"
               : vu_prereq_installed(&c.inv, d.prereq[j].kitname) ? " (installed)"
                                                                 : "");
    for (i = 0; i < c.cat.n; i++) {
        const vu_entry *e = &c.cat.e[i];
        if (e == target || !same_name(e->product, name))
            continue;
        printf("%s %s%s", others++ ? "," : "  Other kits:      ", e->ver,
               vu_ver_is_release(e->ver) ? "" : " (field test)");
    }
    if (others)
        printf("\n");
    printf("\n");
    wrap(d.description[0] ? d.description : target->description, "  ");
    ctx_close(&c);
    return SS$_NORMAL;
}

/* ---------- UPGRADE ---------- */

#define MAX_JOBS 64

typedef struct {
    const vu_entry *e;
    vu_detail d;
    int prereq_only;  /* added as a prerequisite of another kit */
    int reboot;
    char dir[256];    /* kit directory */
    char kitfile[128];
} job_t;

static job_t *jobs;
static size_t njobs;

static int job_index(long package_number)
{
    size_t i;
    for (i = 0; i < njobs; i++)
        if (jobs[i].e->package_number == package_number)
            return (int)i;
    return -1;
}

/* Add a job for entry e; fetches its detail. 0, or a message value. */
static unsigned int add_job(ctx_t *c, const vu_entry *e, int prereq_only)
{
    job_t *j;
    unsigned int st;
    if (job_index(e->package_number) >= 0)
        return 0;
    if (njobs == MAX_JOBS)
        return msg(VU__FAILED, 1, "too many kits in one UPGRADE", 0);
    j = &jobs[njobs];
    memset(j, 0, sizeof *j);
    j->e = e;
    j->prereq_only = prereq_only;
    if ((st = get_detail(c, e->package_number, &j->d)) != 0)
        return st;
    j->reboot = j->d.restart_required == 1;
    njobs++;
    return 0;
}

/* Does a prerequisite apply to this node? Patch details list the variants for
 * several OpenVMS versions (an L2 kit names both VMS842L1A_NOTARY and
 * VMS842L2A_NOTARY, N19): one made for another version is skipped. */
static int prereq_applies(const ctx_t *c, const vu_prereq *q)
{
    char prod[48], ver[48], fam[16];
    size_t k;
    for (k = 0; k < c->cat.n; k++)
        if (c->cat.e[k].package_number == q->package_number)
            return vu_entry_applies(&c->cat.e[k], c->os, c->archbit);
    if (vu_kitname_decode(q->kitname, prod, sizeof prod, ver, sizeof ver)
        && vu_patch_family_os(prod, fam, sizeof fam))
        return same_name(fam, c->os);
    return 1;
}

/* VMS directory for a kit under the work directory: [KITS] + .<kitname>. */
static int kit_dir(const char *work, const char *kitname, char *out, size_t sz)
{
    char sub[80];
    size_t i, n = strlen(work);
    if (n < 2 || work[n - 1] != ']')
        return -1;
    for (i = 0; kitname[i] && i < sizeof sub - 1; i++)
        sub[i] = isalnum((unsigned char)kitname[i]) ? (char)toupper((unsigned char)kitname[i]) : '_';
    sub[i] = '\0';
    if (n + 1 + i + 1 >= sz)
        return -1;
    snprintf(out, sz, "%.*s.%s]", (int)(n - 1), work, sub);
    return 0;
}

static int make_dir(const char *dir)
{
    struct dsc$descriptor_s d;
    dsc(&d, dir, strlen(dir));
    return (lib$create_dir(&d, 0, 0, 0, 0, 0) & 1) ? 0 : -1;
}

/* Delete a kit directory's files and the directory itself. */
static void remove_dir(const char *dir)
{
    char spec[300], cmd[400];
    struct dsc$descriptor_s dc;
    unsigned int flags = CLI$M_NOCLISYM | CLI$M_NOKEYPAD, status = 0;
    size_t n = strlen(dir);
    const char *dot;
    /* DELETE dir*.*;* then the .DIR file in the parent. */
    snprintf(cmd, sizeof cmd, "DELETE/NOLOG/NOCONFIRM %s*.*;*", dir);
    dsc(&dc, cmd, strlen(cmd));
    lib$spawn(&dc, 0, 0, &flags, 0, 0, &status, 0, 0, 0, 0, 0, 0);
    dot = dir + n - 1;
    while (dot > dir && *dot != '.' && *dot != '[')
        dot--;
    if (*dot == '.') {
        snprintf(spec, sizeof spec, "%.*s]%.*s.DIR;1", (int)(dot - dir), dir,
                 (int)(n - 2 - (size_t)(dot - dir)), dot + 1);
        snprintf(cmd, sizeof cmd, "SET SECURITY/PROTECTION=(O:RWED) %s", spec);
        dsc(&dc, cmd, strlen(cmd));
        lib$spawn(&dc, 0, 0, &flags, 0, 0, &status, 0, 0, 0, 0, 0, 0);
        snprintf(cmd, sizeof cmd, "DELETE/NOLOG/NOCONFIRM %s", spec);
        dsc(&dc, cmd, strlen(cmd));
        lib$spawn(&dc, 0, 0, &flags, 0, 0, &status, 0, 0, 0, 0, 0, 0);
    }
}

typedef struct {
    const char *dir;
    int nkits;
    char kitfile[128];
} unzip_ctx;

/* PCSI kit files are written as fixed 512-byte records, as VSI's instructions
 * for transferred kits ask (D16); everything else as a plain stream file. */
static FILE *open_member(const char *name, void *user)
{
    unzip_ctx *u = (unzip_ctx *)user;
    char path[400];
    int kind = vu_kitfile_kind(name);
    snprintf(path, sizeof path, "%s%s", u->dir, name);
    if (kind == VU_KF_PCSI || kind == VU_KF_COMPRESSED) {
        u->nkits++;
        strncpy(u->kitfile, name, sizeof u->kitfile - 1);
        /* open() takes the RMS keywords (fopen is two-argument under /STANDARD=C99). */
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0, "rfm=fix", "mrs=512", "rat=none");
        return fd < 0 ? NULL : fdopen(fd, "wb");
    }
    return fopen(path, "wb");
}

/* Download, verify and unpack one job into its directory. 0 or a message value. */
static unsigned int fetch_job(ctx_t *c, job_t *j, const char *work)
{
    int k;
    if (kit_dir(work, j->e->kitname, j->dir, sizeof j->dir) != 0)
        return msg(VU__KIT, 2, work, "/WORK must be a directory such as DEV:[DIR]");
    if (make_dir(j->dir) != 0)
        return msg(VU__KIT, 2, j->dir, "cannot create the directory");
    for (k = 0; k < j->d.nfiles; k++) {
        const vu_file *f = &j->d.files[k];
        char path[400], hex[33], err[256], size[32];
        unsigned long bytes = 0;
        FILE *out;
        int rc, n;
        unzip_ctx u;
        if (!f->save_as[0] || strpbrk(f->save_as, ":[]<>/\\") || strstr(f->save_as, ".."))
            return msg(VU__KIT, 2, j->e->kitname, "the portal gave an unusable file name");
        snprintf(path, sizeof path, "%s%s", j->dir, f->save_as);
        if (!c->s.user[0] && (cred_status = load_session(&c->s)) != 0)
            return cred_status;
        mb((unsigned long)f->size, size, sizeof size);
        printf("  downloading %s (%s) ...\n", f->save_as, size);
        fflush(stdout);
        if (!(out = fopen(path, "wb")))
            return msg(VU__KIT, 2, path, "cannot create the file");
        rc = vu_portal_download(&c->s, f->file_number, c->os, f->save_as, out, &bytes);
        if (fclose(out) != 0 && rc == VU_OK) {
            rc = VU_E_NET;
            strcpy(c->s.err, "cannot write the kit file (disk full?)");
        }
        if (rc != VU_OK) {
            remove(path);
            return portal_msg(rc, c->s.err);
        }
        if (f->md5[0]) {
            if (vu_md5_file(path, hex) != 0)
                return msg(VU__KIT, 2, path, "cannot read back the download");
            if (!same_name(hex, f->md5))
                return msg(VU__MD5, 2, f->save_as, f->md5);
            printf("  MD5 %s matches\n", hex);
        } else {
            printf("  (the portal gives no MD5 for %s: not verified)\n", f->save_as);
        }
        if (vu_kitfile_kind(f->save_as) != VU_KF_NONE) {
            strncpy(j->kitfile, f->save_as, sizeof j->kitfile - 1);
            continue;
        }
        n = (int)strlen(f->save_as);
        if (!(n > 4 && same_name(f->save_as + n - 4, ".ZIP"))
            && !(n > 7 && same_name(f->save_as + n - 7, ".ZIPEXE")))
            continue; /* release notes and the like stay as they are */
        memset(&u, 0, sizeof u);
        u.dir = j->dir;
        n = vu_unzip(path, open_member, &u, err, sizeof err);
        if (n < 0)
            return msg(VU__KIT, 2, f->save_as, err);
        while (remove(path) == 0)
            ;
        if (u.nkits != 1)
            return msg(VU__KIT, 2, f->save_as,
                       u.nkits ? "holds more than one PCSI kit (not supported)"
                               : "holds no PCSI kit");
        strcpy(j->kitfile, u.kitfile);
    }
    if (!j->kitfile[0])
        return msg(VU__KIT, 2, j->e->kitname, "no PCSI kit among its files");
    printf("  kit: %s%s\n", j->dir, j->kitfile);
    return 0;
}

/* PRODUCT INSTALL in a subprocess on this terminal. 0 or a message value. */
static unsigned int install_job(job_t *j, int confirm)
{
    char producer[32], base[16], product[64], cmd[600];
    struct dsc$descriptor_s dc;
    unsigned int st, status = 0, flags = CLI$M_NOCLISYM | CLI$M_NOKEYPAD;
    if (!vu_kitfile_parse(j->kitfile, producer, sizeof producer, base, sizeof base, product,
                          sizeof product))
        return msg(VU__KIT, 2, j->kitfile, "is not a producer-base-product-version kit name");
    /* SIGNED_KIT: PCSI refuses a kit whose VSI signature doesn't validate (D18).
     * NOCONFIRM skips only PCSI's "these products?" question: the user has already
     * confirmed VUPDATE's list (or gave /NOCONFIRM). Kit questions are still asked. */
    (void)confirm;
    snprintf(cmd, sizeof cmd,
             "PRODUCT INSTALL %s /PRODUCER=%s /BASE_SYSTEM=%s /SOURCE=%s%s /OPTIONS=(SIGNED_KIT,NOCONFIRM)",
             product, producer, base, j->dir,
             j->e->type == VU_T_PATCH ? " /SAVE_RECOVERY_DATA" : "");
    printf("  $ %s\n", cmd);
    fflush(stdout);
    dsc(&dc, cmd, strlen(cmd));
    st = lib$spawn(&dc, 0, 0, &flags, 0, 0, &status, 0, 0, 0, 0, 0, 0);
    if (!(st & 1) || !(status & 1)) {
        char why[80];
        snprintf(why, sizeof why, "PRODUCT INSTALL failed (%%X%08X); the kit is kept",
                 (st & 1) ? status : st);
        return msg(VU__KIT, 2, j->e->kitname, why);
    }
    return 0;
}

static unsigned int do_upgrade(void)
{
    ctx_t c;
    char names[16][64], work[256], line[120], size[32];
    int nnames = 0, all = present("ALL"), only_sec = present("SECURITY");
    int download_only = present("DOWNLOAD_ONLY"), confirm = present("CONFIRM");
    int patches = present("PATCHES"), updates = present("UPDATES");
    int reboot_ok = present("REBOOT"); /* /NOREBOOT: skip kits that need a restart */
    const char *n_items[MAX_JOBS];
    const char *pre_names[MAX_JOBS][VU_MAXPREREQ];
    const char *const *pre_ptr[MAX_JOBS];
    int npre[MAX_JOBS];
    size_t order[MAX_JOBS], i, k;
    unsigned long total = 0;
    unsigned int st = 0;
    int done = 0, m;

    while (nnames < 16 && value("PRODUCT", names[nnames], sizeof names[nnames]))
        nnames++;
    if (!value("WORK", work, sizeof work))
        strcpy(work, "VUPDATE$ROOT:[KITS]");
    ctx_options(&c);
    if ((st = ctx_open(&c)) != 0) {
        ctx_close(&c);
        return st;
    }
    jobs = (job_t *)calloc(MAX_JOBS, sizeof *jobs);
    njobs = 0;
    if (!jobs) {
        ctx_close(&c);
        return msg(VU__FAILED, 1, "out of memory", 0);
    }

    /* Select. */
    for (i = 0; i < c.pl.n && !st; i++) {
        const vu_finding *f = &c.pl.f[i];
        int named = 0;
        for (m = 0; m < nnames; m++)
            if (f->entry && same_name(f->entry->product, names[m]))
                named = 1;
        if (!vu_finding_actionable(f->kind)
            || !(all || named || (only_sec && (c.flags[i] & VU_FL_SECURITY))
                 || (patches && f->kind == VU_F_PATCH) || (updates && f->kind == VU_F_UPDATE)))
            continue;
        if (f->kind == VU_F_OSUPGRADE) {
            printf("  %s %s: an OpenVMS upgrade, reported only (VUPDATE never installs one)\n",
                   f->entry->product, f->entry->ver);
            continue;
        }
        st = add_job(&c, f->entry, 0);
    }
    for (m = 0; m < nnames && !st; m++) {
        int found = 0;
        for (i = 0; i < njobs; i++)
            if (same_name(jobs[i].e->product, names[m]))
                found = 1;
        for (i = 0; i < c.pl.n && !found; i++)
            if (c.pl.f[i].kind == VU_F_CURRENT && c.pl.f[i].installed
                && same_name(c.pl.f[i].installed->name, names[m])) {
                msg(VU__NOTFOUND, 2, names[m], "is up to date");
                found = 1;
            }
        if (!found)
            msg(VU__NOTFOUND, 2, names[m], "no update or patch for this node in the catalogue");
    }
    /* Prerequisites not installed yet, transitively. */
    for (i = 0; i < njobs && !st; i++) {
        int p;
        for (p = 0; p < jobs[i].d.nprereq && !st; p++) {
            const vu_prereq *q = &jobs[i].d.prereq[p];
            if (vu_prereq_installed(&c.inv, q->kitname) || job_index(q->package_number) >= 0
                || !prereq_applies(&c, q))
                continue;
            for (k = 0; k < c.cat.n; k++)
                if (c.cat.e[k].package_number == q->package_number)
                    break;
            if (k == c.cat.n)
                st = msg(VU__KIT, 2, q->kitname, "is a prerequisite but not in the catalogue");
            else
                st = add_job(&c, &c.cat.e[k], 1);
        }
    }
    if (st)
        goto out;
    /* /NOREBOOT: drop kits that need a restart, then (repeatedly) kits that need a
     * dropped kit first. */
    if (!reboot_ok && njobs) {
        int changed = 1;
        size_t w;
        char *drop = (char *)calloc(njobs, 1);
        if (!drop) {
            st = msg(VU__FAILED, 1, "out of memory", 0);
            goto out;
        }
        for (i = 0; i < njobs; i++)
            drop[i] = (char)jobs[i].reboot;
        while (changed) {
            changed = 0;
            for (i = 0; i < njobs; i++) {
                int p;
                if (drop[i])
                    continue;
                for (p = 0; p < jobs[i].d.nprereq; p++) {
                    int x = job_index(jobs[i].d.prereq[p].package_number);
                    if (x >= 0 && drop[x]) {
                        drop[i] = 2;
                        changed = 1;
                        break;
                    }
                }
            }
        }
        for (i = 0, w = 0; i < njobs; i++) {
            if (drop[i]) {
                printf("  skipped (/NOREBOOT): %s%s\n", jobs[i].e->kitname,
                       drop[i] == 2 ? " (needs a kit that needs a reboot)" : " (needs a reboot)");
                continue;
            }
            if (w != i)
                jobs[w] = jobs[i];
            w++;
        }
        njobs = w;
        free(drop);
    }
    if (!njobs) {
        st = msg(VU__NOTHING, 0, 0, 0);
        goto out;
    }

    /* Order: prerequisites first (installation_order is per kit; D17). */
    for (i = 0; i < njobs; i++) {
        int p;
        n_items[i] = jobs[i].e->kitname;
        npre[i] = 0;
        for (p = 0; p < jobs[i].d.nprereq; p++)
            pre_names[i][npre[i]++] = jobs[i].d.prereq[p].kitname;
        pre_ptr[i] = pre_names[i];
    }
    if (vu_install_order(njobs, n_items, npre, pre_ptr, order) != 0) {
        st = msg(VU__FAILED, 1, "the prerequisites form a cycle", 0);
        goto out;
    }
    printf("%s %lu kit%s, in this order:\n", download_only ? "Download" : "Install",
           (unsigned long)njobs, njobs == 1 ? "" : "s");
    for (i = 0; i < njobs; i++) {
        const job_t *j = &jobs[order[i]];
        unsigned long b = 0;
        int f;
        for (f = 0; f < j->d.nfiles; f++)
            b += (unsigned long)j->d.files[f].size;
        total += b;
        mb(b, size, sizeof size);
        printf("  %2lu  %-34s %-12s %10s%s%s\n", (unsigned long)(i + 1), j->e->kitname, j->e->ver,
               size, j->prereq_only ? "  prerequisite" : "", j->reboot ? "  reboot" : "");
    }
    mb(total, size, sizeof size);
    printf("Total %s, into %s\n", size, work);
    if (!download_only && confirm) {
        if (!ask("Install these kits? [NO]: ", 1, line, sizeof line)) {
            st = msg(VU__NOTERM, 1, "UPGRADE without /NOCONFIRM or /DOWNLOAD_ONLY", 0);
            goto out;
        }
        if (toupper((unsigned char)line[0]) != 'Y') {
            st = msg(VU__ABORTED, 1, "nothing installed", 0);
            goto out;
        }
    }
    if (make_dir(work) != 0) {
        st = msg(VU__KIT, 2, work, "cannot create the work directory");
        goto out;
    }

    for (i = 0; i < njobs; i++) {
        job_t *j = &jobs[order[i]];
        printf("\n[%lu/%lu] %s\n", (unsigned long)(i + 1), (unsigned long)njobs, j->e->kitname);
        if ((st = fetch_job(&c, j, work)) != 0)
            goto out;
        if (download_only) {
            done++;
            continue;
        }
        if ((st = install_job(j, confirm)) != 0)
            goto out;
        remove_dir(j->dir);
        done++;
        if (j->reboot && i + 1 < njobs) {
            st = msg(VU__REBOOT, 1, j->e->kitname, 0);
            goto out;
        }
        if (j->reboot)
            msg(VU__REBOOT, 1, j->e->kitname, 0);
    }
    if (download_only)
        st = msg_ns(VU__DOWNLOADED, (unsigned int)done, work);
    else
        st = msg_n(VU__INSTALLED, (unsigned int)done);
out:
    free(jobs);
    jobs = NULL;
    ctx_close(&c);
    return st;
}

/* ---------- HELP ---------- */

/* The help library VUPDATE$ROOT:[HELP]VUPDATE.HLB (src/plat/vms/vupdate.hlp),
 * shown like DCL HELP: interactive prompting on a terminal. */
static unsigned int do_help(void)
{
    char topic[256], line[300];
    static const char lib[] = "VUPDATE$ROOT:[HELP]VUPDATE.HLB";
    struct dsc$descriptor_s dl, dlib;
    unsigned int flags = isatty(0) ? HLP$M_PROMPT : 0, st;
    value("TOPIC", topic, sizeof topic);
    snprintf(line, sizeof line, "VUPDATE %s", topic);
    rtrim(line);
    dsc(&dl, line, strlen(line));
    dsc(&dlib, lib, strlen(lib));
    st = lbr$output_help(lib$put_output, 0, &dl, &dlib, &flags, lib$get_input);
    return (st & 1) ? SS$_NORMAL : st;
}

/* ---------- main ---------- */

int main(void)
{
    char action[32];
    if (!value("ACTION", action, sizeof action))
        return (int)msg(VU__FAILED, 1, "no action given (LOGIN, LOGOUT, CHECK, SHOW, UPGRADE, HELP)", 0);
    if (strncmp(action, "LOGI", 4) == 0)
        return (int)do_login();
    if (strncmp(action, "LOGO", 4) == 0)
        return (int)do_logout();
    if (strncmp(action, "CHEC", 4) == 0)
        return (int)do_check();
    if (strncmp(action, "SHOW", 4) == 0)
        return (int)do_show();
    if (strncmp(action, "UPGR", 4) == 0)
        return (int)do_upgrade();
    if (strncmp(action, "HELP", 4) == 0)
        return (int)do_help();
    return (int)msg(VU__FAILED, 1, "unknown action", 0);
}
