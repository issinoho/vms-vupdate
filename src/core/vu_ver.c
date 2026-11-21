/* vu_ver.c - see vu_ver.h. */
#include "vu_ver.h"

#include <ctype.h>
#include <string.h>

#define MAXTOK 24

typedef struct {
    int isnum;
    unsigned long num;
    char txt[16];
} tok_t;

/* Skip one leading stage letter when a digit follows it. */
static const char *skip_stage(const char *s)
{
    if (isalpha((unsigned char)s[0]) && isdigit((unsigned char)s[1]))
        return s + 1;
    return s;
}

static int tokenise(const char *s, tok_t *t, int max)
{
    int n = 0;
    s = skip_stage(s);
    while (*s && n < max) {
        if (isdigit((unsigned char)*s)) {
            unsigned long v = 0;
            while (isdigit((unsigned char)*s)) {
                if (v < 100000000UL)
                    v = v * 10 + (unsigned long)(*s - '0');
                s++;
            }
            t[n].isnum = 1;
            t[n].num = v;
            t[n].txt[0] = '\0';
            n++;
        } else if (isalpha((unsigned char)*s)) {
            size_t k = 0;
            while (isalpha((unsigned char)*s)) {
                if (k + 1 < sizeof t[n].txt)
                    t[n].txt[k++] = (char)toupper((unsigned char)*s);
                s++;
            }
            t[n].txt[k] = '\0';
            t[n].isnum = 0;
            t[n].num = 0;
            n++;
        } else {
            s++;
        }
    }
    /* Drop trailing zero numbers: V9.2 == V9.2-0. */
    while (n > 0 && t[n - 1].isnum && t[n - 1].num == 0)
        n--;
    return n;
}

int vu_ver_cmp(const char *a, const char *b)
{
    tok_t ta[MAXTOK], tb[MAXTOK];
    int na = tokenise(a, ta, MAXTOK), nb = tokenise(b, tb, MAXTOK);
    int i;
    for (i = 0; i < na && i < nb; i++) {
        if (ta[i].isnum && tb[i].isnum) {
            if (ta[i].num != tb[i].num)
                return ta[i].num < tb[i].num ? -1 : 1;
        } else if (!ta[i].isnum && !tb[i].isnum) {
            int c = strcmp(ta[i].txt, tb[i].txt);
            if (c != 0)
                return c < 0 ? -1 : 1;
        } else {
            return ta[i].isnum ? 1 : -1;
        }
    }
    if (na != nb)
        return na < nb ? -1 : 1;
    return 0;
}

int vu_ver_is_release(const char *v)
{
    if (isalpha((unsigned char)v[0]) && isdigit((unsigned char)v[1]))
        return toupper((unsigned char)v[0]) == 'V';
    return 1;
}

static void put(char *dst, size_t sz, const char *src, size_t len)
{
    if (!dst || sz == 0)
        return;
    if (len >= sz)
        len = sz - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int all_digits(const char *s, size_t len)
{
    size_t i;
    if (len == 0)
        return 0;
    for (i = 0; i < len; i++)
        if (!isdigit((unsigned char)s[i]))
            return 0;
    return 1;
}

/* Strip "VSI-", "HP-", "DEC-" and then "X86VMS-"/"I64VMS-"/"AXPVMS-" (or "_"). */
static const char *strip_prefixes(const char *s)
{
    static const char *const vend[] = { "VSI", "HP", "HPE", "DEC", "CPQ", NULL };
    static const char *const arch[] = { "X86VMS", "I64VMS", "AXPVMS", "ALPHA", NULL };
    int i;
    for (i = 0; vend[i]; i++) {
        size_t l = strlen(vend[i]);
        if (strncmp(s, vend[i], l) == 0 && (s[l] == '-' || s[l] == '_')) {
            s += l + 1;
            break;
        }
    }
    for (i = 0; arch[i]; i++) {
        size_t l = strlen(arch[i]);
        if (strncmp(s, arch[i], l) == 0 && (s[l] == '-' || s[l] == '_')) {
            s += l + 1;
            break;
        }
    }
    return s;
}

/* "VMS" + digit ... "_" within the first 12 characters (as vu_is_os_patch_family). */
static int os_patch_family(const char *s, size_t len)
{
    const char *u;
    if (len < 5 || toupper((unsigned char)s[0]) != 'V' || toupper((unsigned char)s[1]) != 'M'
        || toupper((unsigned char)s[2]) != 'S' || !isdigit((unsigned char)s[3]))
        return 0;
    u = (const char *)memchr(s, '_', len);
    return u != NULL && u - s <= 12;
}

/* Does p start a version field: one stage letter then MMmm or N.N, ended by
 * '-' or end of string? On success *vlen is the length of that field. */
static int version_field(const char *p, size_t *vlen)
{
    const char *q;
    if (!(p[0] == 'V' || p[0] == 'A' || p[0] == 'X' || p[0] == 'T' || p[0] == 'E'
          || p[0] == 'B'))
        return 0;
    q = p + 1;
    if (!isdigit((unsigned char)*q))
        return 0;
    while (isdigit((unsigned char)*q))
        q++;
    if (*q == '.') {
        q++;
        if (!isdigit((unsigned char)*q))
            return 0;
        while (isdigit((unsigned char)*q))
            q++;
    } else if (q - p != 5) {
        return 0; /* packed form must be exactly MMmm */
    } else if (isalpha((unsigned char)*q)) {
        /* "V0909A-2" (portal: V9.9-2A): letters after MMmm. Where they belong
         * isn't knowable from the name, so the caller uses the portal version. */
        while (isalpha((unsigned char)*q))
            q++;
        if (*q != '\0' && *q != '-')
            return 0;
        *vlen = (size_t)(q - p);
        return 2;
    }
    if (*q != '\0' && *q != '-')
        return 0;
    *vlen = (size_t)(q - p);
    return 1;
}

int vu_kitname_decode(const char *kitname, char *product, size_t prodsz,
                      char *ver, size_t versz)
{
    const char *s = strip_prefixes(kitname);
    const char *p;
    size_t vlen = 0;
    char out[VU_VER_MAX];
    size_t o = 0;
    const char *groups[8];
    size_t glen[8];
    int ng = 0, g;
    const char *r;

    int kind = 0;

    /* Find the first "-<version>" field after the product name. */
    for (p = strchr(s, '-'); p; p = strchr(p + 1, '-'))
        if ((kind = version_field(p + 1, &vlen)) != 0)
            break;
    if (!p) {
        put(product, prodsz, s, strlen(s));
        put(ver, versz, "", 0);
        return 0;
    }
    put(product, prodsz, s, (size_t)(p - s));
    if (kind == 2) {
        put(ver, versz, "", 0);
        return 2;
    }
    p++; /* at the stage letter */

    /* Main version: stage + major.minor */
    out[o++] = p[0];
    if (memchr(p, '.', vlen)) {
        size_t k;
        for (k = 1; k < vlen && o + 1 < sizeof out; k++)
            out[o++] = p[k];
    } else {
        int major = (p[1] - '0') * 10 + (p[2] - '0');
        int minor = (p[3] - '0') * 10 + (p[4] - '0');
        char buf[16];
        size_t k;
        /* sprintf-free formatting keeps this file I/O-free */
        k = 0;
        if (major >= 10)
            buf[k++] = (char)('0' + major / 10);
        buf[k++] = (char)('0' + major % 10);
        buf[k++] = '.';
        if (minor >= 10)
            buf[k++] = (char)('0' + minor / 10);
        buf[k++] = (char)('0' + minor % 10);
        buf[k] = '\0';
        for (k = 0; buf[k] && o + 1 < sizeof out; k++)
            out[o++] = buf[k];
    }

    /* Remaining '-' groups: update, extra, kit revision. */
    r = p + vlen;
    while (*r == '-' && ng < 8) {
        const char *e = strchr(r + 1, '-');
        size_t l = e ? (size_t)(e - (r + 1)) : strlen(r + 1);
        groups[ng] = r + 1;
        glen[ng] = l;
        ng++;
        r = r + 1 + l;
    }
    /* A final all-digit group after another group is the kit revision. */
    if (ng >= 2 && all_digits(groups[ng - 1], glen[ng - 1]))
        ng--;
    /* OpenVMS patch families (VMS842L2A_, VMS923X_ ...) are versioned Vmm.nn only:
     * a lone digit after it is the PCSI kit type ("VMS842L2A_TIMEZONE-V0100-4";
     * its kit file is ...-V0100--4 and PCSI records V1.0; N21). */
    if (ng == 1 && glen[0] == 1 && isdigit((unsigned char)groups[0][0])
        && os_patch_family(s, (size_t)(p - 1 - s)))
        ng = 0;
    if (ng > 0) {
        size_t any = 0;
        for (g = 0; g < ng; g++)
            any += glen[g];
        if (any > 0 && o + 1 < sizeof out)
            out[o++] = '-';
        for (g = 0; g < ng; g++) {
            size_t k;
            for (k = 0; k < glen[g] && o + 1 < sizeof out; k++)
                out[o++] = groups[g][k];
        }
    }
    out[o] = '\0';
    put(ver, versz, out, o);
    return 1;
}
