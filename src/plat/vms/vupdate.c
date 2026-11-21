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
#include "vu_pcsi.h"
#include "vu_plan.h"
#include "vu_portal.h"
#include "vu_report.h"
#include "vu_store.h"

#include <climsgdef.h>
#include <clidef.h>
#include <cli$routines.h>
#include <descrip.h>
#include <fcntl.h>
#include <iodef.h>
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

static unsigned int do_check(void)
{
    vu_session s;
    vu_inventory inv;
    vu_catalog cat;
    vu_plan pl;
    char arch_name[32], node[32], os[32], cpath[96], err[512], line[160], outspec[256];
    const char *arch;
    const vu_product *osp;
    char *json = NULL;
    long cat_age;
    int archbit, rc, opts = 0, only_sec, show_all, refresh, *flags = NULL;
    unsigned int st;
    size_t i, nact = 0, nvsi = 0;
    FILE *out = stdout;
    time_t now = time(NULL);
    char when[32];

    only_sec = present("SECURITY");
    show_all = present("ALL");
    refresh = present("REFRESH");
    if (present("FIELD_TEST"))
        opts |= VU_OPT_FIELD_TEST;

    syi_str(SYI$_ARCH_NAME, arch_name, sizeof arch_name);
    syi_str(SYI$_SCSNODE, node, sizeof node);
    archbit = vu_arch_from_name(arch_name);
    arch = vu_arch_portal_name(archbit);
    if (archbit != VU_ARCH_X86 && archbit != VU_ARCH_AXP)
        return msg(VU__FAILED, 1, "VUPDATE supports x86-64 and Alpha only", 0);

    memset(&s, 0, sizeof s); /* signed in on the first portal request */
    if ((st = inventory(&inv)) != 0)
        return st;
    /* The running OpenVMS version as PCSI names it (V8.4-2L2, E9.2-4). */
    /* Both have the platform product OPENVMS; Alpha also the OS product VMS. */
    osp = vu_inventory_find(&inv, "OPENVMS");
    if (!osp || !osp->ver[0])
        osp = vu_inventory_find(&inv, "VMS");
    if (osp && osp->ver[0])
        strcpy(os, osp->ver);
    else
        syi_str(SYI$_VERSION, os, sizeof os);

    if (http_start() != 0) {
        vu_session_end(&s);
        vu_inventory_free(&inv);
        return msg(VU__FAILED, 1, "cannot initialise libcurl", 0);
    }
    snprintf(cpath, sizeof cpath, "VUPDATE$ROOT:[CACHE]CATALOG_%s.JSON", arch);
    rc = cached(&s, cpath, CAT_MAX_AGE, refresh, fetch_cat, arch, &json, &cat_age);
    if (rc != VU_OK) {
        st = portal_msg(rc, s.err);
        goto out_session;
    }
    rc = vu_catalog_parse(json, &cat, err, sizeof err);
    free(json);
    json = NULL;
    if (rc != 0) {
        remove(cpath);
        st = rc == -3 ? msg(VU__PORTAL, 1, err, 0) : msg(VU__BADREPLY, 1, "catalogue", 0);
        goto out_session;
    }

    /* Catalogue rows carry only the first minos of a patch (D7): merge the
     * detail's list for kits that may also cover this version. */
    for (i = 0; i < cat.n; i++) {
        vu_detail d;
        char dpath[96];
        long age;
        if (!vu_patch_needs_detail(&cat.e[i], os))
            continue;
        snprintf(dpath, sizeof dpath, "VUPDATE$ROOT:[CACHE]DETAIL_%ld.JSON",
                 cat.e[i].package_number);
        rc = cached(&s, dpath, DETAIL_MAX_AGE, refresh, fetch_det, &cat.e[i].package_number,
                    &json, &age);
        if (rc != VU_OK) {
            st = portal_msg(rc, s.err);
            goto out_cat;
        }
        rc = vu_detail_parse(json, &d, err, sizeof err);
        free(json);
        json = NULL;
        if (rc != 0) {
            remove(dpath);
            st = rc == -3 ? msg(VU__PORTAL, 1, err, 0) : msg(VU__BADREPLY, 1, "package detail", 0);
            goto out_cat;
        }
        vu_entry_set_minos(&cat.e[i], &d);
    }

    if (vu_plan_build(&inv, &cat, os, archbit, opts, &pl) != 0) {
        st = msg(VU__FAILED, 1, "out of memory", 0);
        goto out_cat;
    }
    flags = (int *)calloc(pl.n ? pl.n : 1, sizeof *flags);
    if (!flags) {
        st = msg(VU__FAILED, 1, "out of memory", 0);
        goto out_plan;
    }
    /* Flags: patches from their detail (security text, restart_required); updates
     * from the catalogue description. */
    for (i = 0; i < pl.n; i++) {
        const vu_finding *f = &pl.f[i];
        if (!f->entry || !vu_finding_actionable(f->kind))
            continue;
        if (f->kind == VU_F_PATCH) {
            vu_detail d;
            char dpath[96];
            long age, num = f->entry->package_number;
            snprintf(dpath, sizeof dpath, "VUPDATE$ROOT:[CACHE]DETAIL_%ld.JSON", num);
            rc = cached(&s, dpath, DETAIL_MAX_AGE, refresh, fetch_det, &num, &json, &age);
            if (rc != VU_OK) {
                st = portal_msg(rc, s.err);
                goto out_flags;
            }
                rc = vu_detail_parse(json, &d, err, sizeof err);
            free(json);
            json = NULL;
            if (rc != 0) {
                remove(dpath);
                st = msg(VU__BADREPLY, 1, "package detail", 0);
                goto out_flags;
            }
            flags[i] = VU_FL_DETAIL | (d.security ? VU_FL_SECURITY : 0)
                       | (d.restart_required == 1 ? VU_FL_REBOOT : 0);
        } else if (vu_text_is_security(f->entry->description)) {
            flags[i] = VU_FL_SECURITY;
        }
    }

    if (value("OUTPUT", outspec, sizeof outspec)) {
        out = fopen(outspec, "w");
        if (!out) {
            st = msg(VU__FAILED, 1, "cannot create the /OUTPUT file", 0);
            goto out_flags;
        }
    }
    strftime(when, sizeof when, "%d-%b-%Y %H:%M", localtime(&now));
    fprintf(out, "VUPDATE CHECK on %s: OpenVMS %s %s, %s\n", node, os, arch_name, when);
    if (cat_age >= 0)
        fprintf(out, "VSI catalogue: %lu kits (cached %ld min ago; /REFRESH to fetch now)\n",
                (unsigned long)cat.n, cat_age / 60);
    else
        fprintf(out, "VSI catalogue: %lu kits (fetched now)\n", (unsigned long)cat.n);
    if (only_sec)
        fprintf(out, "Showing security fixes only (/SECURITY).\n");
    {
        /* Report order (vu_plan's sort order), not the enum's. */
        static const vu_find_t order[] = {VU_F_OSUPGRADE, VU_F_PATCH, VU_F_UPDATE,
                                          VU_F_CURRENT, VU_F_NOTINCAT};
        size_t k;
        for (k = 0; k < sizeof order / sizeof order[0]; k++) {
            int head = 0;
            for (i = 0; i < pl.n; i++) {
                const vu_finding *f = &pl.f[i];
                if (f->kind != order[k])
                    continue;
                if (!vu_finding_actionable(f->kind) && !show_all)
                    continue;
                if (only_sec && !(flags[i] & VU_FL_SECURITY))
                    continue;
                if (!head) {
                    fprintf(out, "\n%s\n", vu_report_heading(f->kind));
                    head = 1;
                }
                vu_report_line(f, flags[i], line, sizeof line);
                fprintf(out, "%s\n", line);
            }
        }
    }
    for (i = 0; i < pl.n; i++) {
        if (pl.f[i].installed && pl.f[i].kind != VU_F_PATCH)
            nvsi++;
        if (vu_finding_actionable(pl.f[i].kind) && (!only_sec || (flags[i] & VU_FL_SECURITY)))
            nact++;
    }
    if (!show_all)
        fprintf(out, "\n(/ALL also lists the products that are up to date or not in the "
                     "catalogue.)\n");
    if (out != stdout) {
        fclose(out);
        out = stdout;
    }
    st = nact ? msg_n(VU__UPDATES, (unsigned int)nact) : msg_n(VU__CURRENT, (unsigned int)nvsi);

out_flags:
    free(flags);
out_plan:
    vu_plan_free(&pl);
out_cat:
    vu_catalog_free(&cat);
out_session:
    free(json);
    vu_http_cleanup();
    vu_session_end(&s);
    vu_inventory_free(&inv);
    return st;
}

/* ---------- main ---------- */

int main(void)
{
    char action[32];
    if (!value("ACTION", action, sizeof action))
        return (int)msg(VU__FAILED, 1, "no action given (LOGIN, LOGOUT, CHECK, SHOW, UPGRADE)", 0);
    if (strncmp(action, "LOGI", 4) == 0)
        return (int)do_login();
    if (strncmp(action, "LOGO", 4) == 0)
        return (int)do_logout();
    if (strncmp(action, "CHEC", 4) == 0)
        return (int)do_check();
    if (strncmp(action, "SHOW", 4) == 0)
        return (int)msg(VU__NYI, 1, "VUPDATE SHOW", 0);
    if (strncmp(action, "UPGR", 4) == 0)
        return (int)msg(VU__NYI, 1, "VUPDATE UPGRADE", 0);
    return (int)msg(VU__FAILED, 1, "unknown action", 0);
}
