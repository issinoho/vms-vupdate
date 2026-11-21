/* vu_pcsi.c - see vu_pcsi.h.
 *
 * Layout of PRODUCT SHOW PRODUCT * /FULL (OpenVMS x86 E9.2-4, Alpha V8.4-2L2):
 *
 *   ------------------------------------ ----------- --------- ----------- ...
 *   PRODUCT                              KIT TYPE    STATE     MAINTENANCE ...
 *   ------------------------------------ ----------- --------- ----------- ...
 *   VSI AXPVMS VMS V8.4-2L2              Oper System Installed VSI AXPVMS VMS842L2A_DPML V2.0 ...
 *                                                              VSI AXPVMS VMS842L2A_NOTARY V2.0
 *
 * Column positions come from the dash line under the header, so terminal width
 * and the product-name column width don't matter.
 */
#include "vu_pcsi.h"
#include "vu_ver.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAXCOL 8

typedef struct {
    size_t start, end; /* [start, end) */
} col_t;

static int ieq_prefix(const char *s, const char *p)
{
    while (*p) {
        if (toupper((unsigned char)*s) != toupper((unsigned char)*p))
            return 0;
        s++;
        p++;
    }
    return 1;
}

static int is_dash_line(const char *s, size_t len)
{
    size_t i, dashes = 0;
    for (i = 0; i < len; i++) {
        if (s[i] == '-')
            dashes++;
        else if (s[i] != ' ')
            return 0;
    }
    return dashes >= 10;
}

static int split_cols(const char *s, size_t len, col_t *c, int max)
{
    size_t i = 0;
    int n = 0;
    while (i < len && n < max) {
        while (i < len && s[i] == ' ')
            i++;
        if (i >= len)
            break;
        c[n].start = i;
        while (i < len && s[i] == '-')
            i++;
        c[n].end = i;
        n++;
    }
    return n;
}

/* Copy the trimmed text of column c from a line into buf. */
static void cell(const char *line, size_t len, col_t c, size_t next_start, char *buf,
                 size_t sz)
{
    size_t a = c.start, b = next_start ? next_start : len, k = 0;
    if (a >= len) {
        buf[0] = '\0';
        return;
    }
    if (b > len)
        b = len;
    while (a < b && line[a] == ' ')
        a++;
    while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\r'))
        b--;
    while (a < b && k + 1 < sz)
        buf[k++] = line[a++];
    buf[k] = '\0';
}

static int push(vu_inventory *inv, const vu_product *p)
{
    if (inv->n == inv->cap) {
        size_t nc = inv->cap ? inv->cap * 2 : 64;
        vu_product *np = (vu_product *)realloc(inv->p, nc * sizeof *np);
        if (!np)
            return -2;
        inv->p = np;
        inv->cap = nc;
    }
    inv->p[inv->n++] = *p;
    return 0;
}

static void copyw(char *dst, size_t sz, const char *s, size_t len)
{
    if (len >= sz)
        len = sz - 1;
    memcpy(dst, s, len);
    dst[len] = '\0';
}

int vu_pcsi_parse_spec(const char *spec, size_t len, vu_product *out)
{
    const char *w[4];
    size_t wl[4];
    int n = 0;
    size_t i = 0;
    memset(out, 0, sizeof *out);
    while (i < len && n < 5) {
        while (i < len && spec[i] == ' ')
            i++;
        if (i >= len)
            break;
        if (n == 4)
            return 0; /* more than four words: not a product spec */
        w[n] = spec + i;
        while (i < len && spec[i] != ' ')
            i++;
        wl[n] = (size_t)(spec + i - w[n]);
        n++;
    }
    if (n != 4)
        return 0;
    if (!(isalpha((unsigned char)w[3][0]) && isdigit((unsigned char)w[3][1])))
        return 0;
    copyw(out->producer, sizeof out->producer, w[0], wl[0]);
    copyw(out->base, sizeof out->base, w[1], wl[1]);
    copyw(out->name, sizeof out->name, w[2], wl[2]);
    copyw(out->ver, sizeof out->ver, w[3], wl[3]);
    return 1;
}

static vu_kit_t kit_type(const char *s)
{
    if (ieq_prefix(s, "Full"))
        return VU_KIT_FULL;
    if (ieq_prefix(s, "Patch"))
        return VU_KIT_PATCH;
    if (ieq_prefix(s, "Platform"))
        return VU_KIT_PLATFORM;
    if (ieq_prefix(s, "Oper"))
        return VU_KIT_OS;
    return VU_KIT_OTHER;
}

const vu_product *vu_inventory_find(const vu_inventory *inv, const char *name)
{
    size_t i;
    for (i = 0; i < inv->n; i++) {
        const char *a = inv->p[i].name, *b = name;
        while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return &inv->p[i];
    }
    return NULL;
}

static int add_maint(vu_inventory *inv, const char *spec)
{
    vu_product p;
    size_t i;
    if (!vu_pcsi_parse_spec(spec, strlen(spec), &p))
        return 0;
    for (i = 0; i < inv->n; i++)
        if (strcmp(inv->p[i].name, p.name) == 0 && strcmp(inv->p[i].ver, p.ver) == 0
            && strcmp(inv->p[i].producer, p.producer) == 0)
            return 0;
    p.kit = VU_KIT_PATCH;
    p.maintenance = 1;
    return push(inv, &p);
}

int vu_pcsi_parse(const char *text, vu_inventory *inv)
{
    const char *line = text;
    col_t col[MAXCOL];
    int ncol = 0, state = 0; /* 0: before header, 1: header seen, 2: in table */
    int c_kit = -1, c_maint = -1;
    char hdr[256];
    size_t hlen = 0;
    int rc;

    inv->p = NULL;
    inv->n = inv->cap = 0;

    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r')
            len--;

        if (state == 0 && len >= 7 && strncmp(line, "PRODUCT", 7) == 0) {
            hlen = len < sizeof hdr - 1 ? len : sizeof hdr - 1;
            memcpy(hdr, line, hlen);
            hdr[hlen] = '\0';
            state = 1;
        } else if (state == 1 && is_dash_line(line, len)) {
            int i;
            ncol = split_cols(line, len, col, MAXCOL);
            for (i = 0; i < ncol; i++) {
                if (col[i].start < hlen && ieq_prefix(hdr + col[i].start, "KIT TYPE"))
                    c_kit = i;
                if (col[i].start < hlen && ieq_prefix(hdr + col[i].start, "MAINTENANCE"))
                    c_maint = i;
            }
            state = 2;
        } else if (state == 2) {
            char buf[128];
            if (is_dash_line(line, len))
                break; /* end of table */
            if (ncol > 0 && len > 0 && line[0] != ' ') {
                vu_product p;
                cell(line, len, col[0], c_kit > 0 ? col[c_kit].start : 0, buf, sizeof buf);
                if (vu_pcsi_parse_spec(buf, strlen(buf), &p)) {
                    char kt[32];
                    if (c_kit >= 0) {
                        cell(line, len, col[c_kit], c_kit + 1 < ncol ? col[c_kit + 1].start : 0,
                             kt, sizeof kt);
                        p.kit = kit_type(kt);
                    } else {
                        p.kit = VU_KIT_OTHER;
                    }
                    if ((rc = push(inv, &p)) != 0)
                        return rc;
                }
            }
            if (c_maint >= 0 && len > col[c_maint].start) {
                cell(line, len, col[c_maint], c_maint + 1 < ncol ? col[c_maint + 1].start : 0,
                     buf, sizeof buf);
                if (buf[0] && (rc = add_maint(inv, buf)) != 0)
                    return rc;
            }
        }
        if (!nl)
            break;
        line = nl + 1;
    }
    return state == 2 ? 0 : -1;
}

void vu_inventory_free(vu_inventory *inv)
{
    free(inv->p);
    inv->p = NULL;
    inv->n = inv->cap = 0;
}

static int ends_ci(const char *s, size_t n, const char *suffix)
{
    size_t l = strlen(suffix), i;
    if (n < l)
        return 0;
    for (i = 0; i < l; i++)
        if (toupper((unsigned char)s[n - l + i]) != toupper((unsigned char)suffix[i]))
            return 0;
    return 1;
}

static size_t name_len(const char *f)
{
    const char *semi = strchr(f, ';');
    return semi ? (size_t)(semi - f) : strlen(f);
}

int vu_kitfile_kind(const char *f)
{
    size_t n = name_len(f);
    if (ends_ci(f, n, ".PCSI$COMPRESSED_ESW") || ends_ci(f, n, ".PCSI_ESW")
        || ends_ci(f, n, ".PCSI$COMPRESSED_VNC") || ends_ci(f, n, ".PCSI_VNC"))
        return VU_KF_MANIFEST;
    if (ends_ci(f, n, ".PCSI$COMPRESSED"))
        return VU_KF_COMPRESSED;
    if (ends_ci(f, n, ".PCSI"))
        return VU_KF_PCSI;
    return VU_KF_NONE;
}

int vu_kitfile_parse(const char *f, char *producer, size_t prodsz, char *base, size_t basesz,
                     char *product, size_t productsz)
{
    char stem[128], ver[48];
    const char *d1, *d2, *dot;
    size_t n = name_len(f);
    dot = memchr(f, '.', n);
    if (dot)
        n = (size_t)(dot - f);
    if (n >= sizeof stem)
        return 0;
    memcpy(stem, f, n);
    stem[n] = '\0';
    d1 = strchr(stem, '-');
    d2 = d1 ? strchr(d1 + 1, '-') : NULL;
    if (!d1 || !d2 || (size_t)(d1 - stem) >= prodsz || (size_t)(d2 - d1 - 1) >= basesz)
        return 0;
    memcpy(producer, stem, (size_t)(d1 - stem));
    producer[d1 - stem] = '\0';
    memcpy(base, d1 + 1, (size_t)(d2 - d1 - 1));
    base[d2 - d1 - 1] = '\0';
    /* The product is what the portal kit-name decoder finds after the prefixes. */
    if (vu_kitname_decode(d2 + 1, product, productsz, ver, sizeof ver) == 0 || !product[0])
        return 0;
    return 1;
}
