/* vu_plan.c - see vu_plan.h. */
#include "vu_plan.h"
#include "vu_ver.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int ieq(const char *a, const char *b)
{
    while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int vu_producer_is_vsi(const char *p)
{
    return ieq(p, "VSI") || ieq(p, "HP") || ieq(p, "HPE") || ieq(p, "DEC") || ieq(p, "CPQ")
           || ieq(p, "COMPAQ");
}

int vu_is_os_patch_family(const char *name)
{
    /* VMS842L2A_SYS, VMS923X_UPDATE, VMS922X_PCSI: "VMS", digits, a code, "_". */
    const char *u;
    if (!(toupper((unsigned char)name[0]) == 'V' && toupper((unsigned char)name[1]) == 'M'
          && toupper((unsigned char)name[2]) == 'S' && isdigit((unsigned char)name[3])))
        return 0;
    u = strchr(name, '_');
    return u != NULL && u - name <= 12;
}

int vu_patch_family_os(const char *name, char *buf, size_t sz)
{
    /* VMS + 3 digits (major, minor, update) + optional L<digit> + arch letter + "_" */
    char out[16];
    size_t o = 0;
    const char *p = name + 3;
    if (!vu_is_os_patch_family(name) || !isdigit((unsigned char)p[1])
        || !isdigit((unsigned char)p[2]))
        return 0;
    out[o++] = 'V';
    out[o++] = p[0];
    out[o++] = '.';
    out[o++] = p[1];
    out[o++] = '-';
    out[o++] = p[2];
    p += 3;
    if (toupper((unsigned char)p[0]) == 'L' && isdigit((unsigned char)p[1])) {
        out[o++] = 'L';
        out[o++] = p[1];
        p += 2;
    }
    if (!isalpha((unsigned char)p[0]) || p[1] != '_')
        return 0;
    out[o] = '\0';
    if (o + 1 > sz)
        return 0;
    memcpy(buf, out, o + 1);
    return 1;
}

int vu_patch_needs_detail(const vu_entry *e, const char *os_ver)
{
    char fam[16];
    if (e->type != VU_T_PATCH || e->nminos != 1 || ieq(e->minos[0], os_ver))
        return 0;
    if (vu_patch_family_os(e->product, fam, sizeof fam) && ieq(fam, os_ver))
        return 0; /* already known to apply */
    /* Only kits made for an earlier update of the same major.minor can also list
     * this version: V9.2-2 kits may cover V9.2-3; V8.4-2L1 kits may cover L2. */
    if (vu_ver_cmp(e->minos[0], os_ver) > 0)
        return 0;
    {
        const char *a = e->minos[0] + 1, *b = os_ver + 1;
        const char *da = strchr(a, '-'), *db = strchr(b, '-');
        size_t la = da ? (size_t)(da - a) : strlen(a), lb = db ? (size_t)(db - b) : strlen(b);
        return la == lb && strncmp(a, b, la) == 0;
    }
}

int vu_entry_applies(const vu_entry *e, const char *os_ver, int arch)
{
    int i;
    const char *lowest = NULL;
    if (arch && e->arch && !(e->arch & arch))
        return 0;
    if (e->type == VU_T_OS || e->nminos == 0)
        return 1;
    if (e->type == VU_T_PATCH) {
        char fam[16];
        for (i = 0; i < e->nminos; i++)
            if (ieq(e->minos[i], os_ver))
                return 1;
        /* Catalogue rows carry only the first minos; the family code
         * (VMS923X_ = V9.2-3) names the release the kit was made for. */
        if (vu_patch_family_os(e->product, fam, sizeof fam) && ieq(fam, os_ver))
            return 1;
        return 0;
    }
    for (i = 0; i < e->nminos; i++) {
        const char *m = e->minos[i];
        if (!(isalpha((unsigned char)m[0]) && isdigit((unsigned char)m[1])))
            continue; /* "windows" and the like */
        if (!lowest || vu_ver_cmp(m, lowest) < 0)
            lowest = m;
    }
    if (!lowest)
        return 0; /* only non-VMS minos values: not for this node */
    return vu_ver_cmp(os_ver, lowest) >= 0;
}

static int push(vu_plan *pl, vu_find_t k, const vu_product *p, const vu_entry *e)
{
    if (pl->n == pl->cap) {
        size_t nc = pl->cap ? pl->cap * 2 : 64;
        vu_finding *nf = (vu_finding *)realloc(pl->f, nc * sizeof *nf);
        if (!nf)
            return -2;
        pl->f = nf;
        pl->cap = nc;
    }
    pl->f[pl->n].kind = k;
    pl->f[pl->n].installed = p;
    pl->f[pl->n].entry = e;
    pl->n++;
    return 0;
}

/* Is candidate c acceptable as an upgrade target for installed version iv? */
static int stage_ok(const char *cand, const char *iv, int opts)
{
    if (vu_ver_is_release(cand))
        return 1;
    if (opts & VU_OPT_FIELD_TEST)
        return 1;
    /* Someone running a field-test kit wants newer field-test kits of it. */
    return iv && !vu_ver_is_release(iv);
}

/* Newest applicable entry for product name (case-insensitive); types limits the
 * portal types considered (string of letters). */
static const vu_entry *newest(const vu_catalog *cat, const char *name, const char *types,
                              const char *os_ver, int arch, int opts, const char *iv)
{
    const vu_entry *best = NULL;
    size_t i;
    for (i = 0; i < cat->n; i++) {
        const vu_entry *e = &cat->e[i];
        if (!strchr(types, e->type) || !ieq(e->product, name) || !e->ver[0])
            continue;
        if (!vu_entry_applies(e, os_ver, arch) || !stage_ok(e->ver, iv, opts))
            continue;
        if (!best || vu_ver_cmp(e->ver, best->ver) > 0)
            best = e;
    }
    return best;
}

static int any_entry(const vu_catalog *cat, const char *name)
{
    size_t i;
    for (i = 0; i < cat->n; i++)
        if (ieq(cat->e[i].product, name))
            return 1;
    return 0;
}

static int rank(vu_find_t k)
{
    switch (k) {
    case VU_F_OSUPGRADE: return 0;
    case VU_F_PATCH: return 1;
    case VU_F_UPDATE: return 2;
    case VU_F_CURRENT: return 3;
    default: return 4;
    }
}

static const char *fname(const vu_finding *f)
{
    return f->installed ? f->installed->name : f->entry ? f->entry->product : "";
}

static int cmp_find(const void *a, const void *b)
{
    const vu_finding *x = (const vu_finding *)a, *y = (const vu_finding *)b;
    int r = rank(x->kind) - rank(y->kind);
    return r ? r : strcmp(fname(x), fname(y));
}

/* Was the patch family already handled (a newer or equal kit installed or queued)? */
static int family_seen(const vu_plan *pl, const char *family)
{
    size_t i;
    for (i = 0; i < pl->n; i++)
        if (pl->f[i].kind == VU_F_PATCH && pl->f[i].entry && ieq(pl->f[i].entry->product, family))
            return 1;
    return 0;
}

int vu_plan_build(const vu_inventory *inv, const vu_catalog *cat, const char *os_ver,
                  int arch, int opts, vu_plan *pl)
{
    size_t i;
    int rc;
    pl->f = NULL;
    pl->n = pl->cap = 0;

    /* Installed products (not patches): updates, OS upgrades, current, not in catalogue. */
    for (i = 0; i < inv->n; i++) {
        const vu_product *p = &inv->p[i];
        const vu_entry *e;
        int is_os;
        if (p->kit == VU_KIT_PATCH || !vu_producer_is_vsi(p->producer))
            continue;
        /* The "Oper System" product (VMS) is never in the catalogue: the platform
         * product OPENVMS stands for the OS, so VMS isn't reported at all. */
        if (p->kit == VU_KIT_OS)
            continue;
        is_os = ieq(p->name, "OPENVMS");
        e = newest(cat, p->name, is_os ? "V" : "LOE", os_ver, arch, opts, p->ver);
        if (e && vu_ver_cmp(e->ver, p->ver) > 0)
            rc = push(pl, is_os ? VU_F_OSUPGRADE : VU_F_UPDATE, p, e);
        else if (e || any_entry(cat, p->name))
            rc = push(pl, VU_F_CURRENT, p, e);
        else
            rc = push(pl, VU_F_NOTINCAT, p, NULL);
        if (rc)
            return rc;
    }

    /* Patches: newest applicable kit per family, unless that or newer is installed.
     * Patch kits for layered products (e.g. PERFMGMT ECO) only when the product
     * itself is installed. */
    for (i = 0; i < cat->n; i++) {
        const vu_entry *e = &cat->e[i], *best;
        const vu_product *have;
        if (e->type != VU_T_PATCH || family_seen(pl, e->product))
            continue;
        best = newest(cat, e->product, "E", os_ver, arch, opts, NULL);
        if (!best)
            continue;
        have = vu_inventory_find(inv, best->product);
        if (!have && !vu_is_os_patch_family(best->product))
            continue; /* ECO for a layered product that isn't installed */
        if (have && vu_ver_cmp(best->ver, have->ver) <= 0)
            continue;
        if ((rc = push(pl, VU_F_PATCH, have, best)) != 0)
            return rc;
    }

    if (pl->n > 1)
        qsort(pl->f, pl->n, sizeof *pl->f, cmp_find);
    return 0;
}

void vu_plan_free(vu_plan *pl)
{
    free(pl->f);
    pl->f = NULL;
    pl->n = pl->cap = 0;
}

int vu_prereq_installed(const vu_inventory *inv, const char *kitname)
{
    char prod[48], ver[48];
    const vu_product *p;
    if (vu_kitname_decode(kitname, prod, sizeof prod, ver, sizeof ver) == 0)
        return 0;
    p = vu_inventory_find(inv, prod);
    if (!p)
        return 0;
    return !ver[0] || vu_ver_cmp(p->ver, ver) >= 0;
}

int vu_install_order(size_t n, const char *const *items, const int *nprereq,
                     const char *const *const *prereq, size_t *order)
{
    /* Simple repeated scan (n is small): place any item whose in-set prerequisites
     * are all placed, earliest input first. */
    unsigned char *placed = (unsigned char *)calloc(n ? n : 1, 1);
    size_t done = 0, i;
    if (!placed)
        return -2;
    while (done < n) {
        int progress = 0;
        for (i = 0; i < n; i++) {
            int j, ready = 1;
            if (placed[i])
                continue;
            for (j = 0; j < nprereq[i] && ready; j++) {
                size_t k;
                for (k = 0; k < n; k++)
                    if (k != i && !placed[k] && ieq(items[k], prereq[i][j]))
                        ready = 0;
            }
            if (ready) {
                placed[i] = 1;
                order[done++] = i;
                progress = 1;
                break; /* restart so earlier items win ties */
            }
        }
        if (!progress) {
            for (i = 0; i < n; i++)
                order[i] = i;
            free(placed);
            return -1;
        }
    }
    free(placed);
    return 0;
}
