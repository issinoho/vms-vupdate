/* vu_cat.c - see vu_cat.h. */
#include "vu_cat.h"
#include "vu_ver.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static void scopy(char *dst, size_t sz, const char *src)
{
    size_t l;
    if (!src)
        src = "";
    l = strlen(src);
    if (l >= sz)
        l = sz - 1;
    memcpy(dst, src, l);
    dst[l] = '\0';
}

static const char *jstr(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(v) && v->valuestring)
        return strcmp(v->valuestring, "None") == 0 ? "" : v->valuestring;
    return "";
}

/* Integer that the portal sends either as a number or as a string ("81", "1"). */
static long jlong(const cJSON *o, const char *key, long dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(v))
        return (long)v->valuedouble;
    if (cJSON_IsString(v) && v->valuestring && isdigit((unsigned char)v->valuestring[0]))
        return strtol(v->valuestring, NULL, 10);
    return dflt;
}

int vu_arch_from_name(const char *a)
{
    if (!a)
        return 0;
    if (toupper((unsigned char)a[0]) == 'X')
        return VU_ARCH_X86;
    if (toupper((unsigned char)a[0]) == 'I')
        return VU_ARCH_I64;
    if (toupper((unsigned char)a[0]) == 'A')
        return VU_ARCH_AXP;
    return 0;
}

const char *vu_arch_portal_name(int arch)
{
    switch (arch) {
    case VU_ARCH_X86: return "X86";
    case VU_ARCH_I64: return "I64";
    case VU_ARCH_AXP: return "AXP";
    default: return "";
    }
}

static int has_ci(const char *s, const char *w)
{
    size_t lw = strlen(w);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < lw; i++)
            if (toupper((unsigned char)s[i]) != toupper((unsigned char)w[i]))
                break;
        if (i == lw)
            return 1;
    }
    return 0;
}

int vu_text_is_security(const char *t)
{
    return has_ci(t, "CVE-") || has_ci(t, "security") || has_ci(t, "vulnerab")
           || has_ci(t, "exploit");
}

static void fill_entry(const cJSON *o, vu_entry *e)
{
    const cJSON *a;
    memset(e, 0, sizeof *e);
    scopy(e->kitname, sizeof e->kitname, jstr(o, "name"));
    scopy(e->portal_ver, sizeof e->portal_ver, jstr(o, "version"));
    if (vu_kitname_decode(e->kitname, e->product, sizeof e->product, e->ver, sizeof e->ver)
        != 1)
        scopy(e->ver, sizeof e->ver, e->portal_ver);
    {
        const char *t = jstr(o, "type");
        e->type = t[0] ? t[0] : '?';
    }
    a = cJSON_GetObjectItemCaseSensitive(o, "sysarch");
    if (cJSON_IsArray(a)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, a) if (cJSON_IsString(it)) e->arch |=
            vu_arch_from_name(strcmp(it->valuestring, "AXP") == 0 ? "Alpha" : it->valuestring);
    }
    /* minos is a string in catalogue rows and an array in detail replies. */
    a = cJSON_GetObjectItemCaseSensitive(o, "minos");
    if (cJSON_IsArray(a)) {
        const cJSON *it;
        cJSON_ArrayForEach(it, a)
        {
            if (cJSON_IsString(it) && e->nminos < VU_MAXMINOS)
                scopy(e->minos[e->nminos++], sizeof e->minos[0], it->valuestring);
        }
    } else if (cJSON_IsString(a) && a->valuestring[0]) {
        scopy(e->minos[0], sizeof e->minos[0], a->valuestring);
        e->nminos = 1;
    }
    e->package_number = jlong(o, "package_number", -1);
    scopy(e->release_date, sizeof e->release_date, jstr(o, "release_date"));
    e->verified = (int)jlong(o, "verified", 0);
    scopy(e->description, sizeof e->description, jstr(o, "description"));
}

static int portal_error(const cJSON *root, char *err, size_t errsz)
{
    if (cJSON_IsObject(root)) {
        if (err && errsz)
            scopy(err, errsz, jstr(root, "reason"));
        return -3;
    }
    if (err && errsz)
        scopy(err, errsz, "unexpected reply shape");
    return -1;
}

int vu_catalog_parse(const char *json, vu_catalog *cat, char *err, size_t errsz)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *it;
    size_t n, i = 0;
    int rc;
    cat->e = NULL;
    cat->n = 0;
    if (err && errsz)
        err[0] = '\0';
    if (!root) {
        if (err && errsz)
            scopy(err, errsz, "reply is not JSON");
        return -1;
    }
    if (!cJSON_IsArray(root)) {
        rc = portal_error(root, err, errsz);
        cJSON_Delete(root);
        return rc;
    }
    n = (size_t)cJSON_GetArraySize(root);
    cat->e = (vu_entry *)calloc(n ? n : 1, sizeof *cat->e);
    if (!cat->e) {
        cJSON_Delete(root);
        return -2;
    }
    cJSON_ArrayForEach(it, root)
    {
        if (cJSON_IsObject(it))
            fill_entry(it, &cat->e[i++]);
    }
    cat->n = i;
    cJSON_Delete(root);
    return 0;
}

void vu_catalog_free(vu_catalog *cat)
{
    free(cat->e);
    cat->e = NULL;
    cat->n = 0;
}

int vu_detail_parse(const char *json, vu_detail *d, char *err, size_t errsz)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *o, *a, *it;
    long v;
    memset(d, 0, sizeof *d);
    if (err && errsz)
        err[0] = '\0';
    if (!root) {
        if (err && errsz)
            scopy(err, errsz, "reply is not JSON");
        return -1;
    }
    if (!cJSON_IsArray(root) || cJSON_GetArraySize(root) < 1) {
        int rc = portal_error(root, err, errsz);
        cJSON_Delete(root);
        return rc;
    }
    o = cJSON_GetArrayItem(root, 0);
    fill_entry(o, &d->entry);
    scopy(d->description, sizeof d->description, jstr(o, "description"));
    v = jlong(o, "restart_required", -1);
    d->restart_required = v < 0 ? -1 : (v ? 1 : 0);
    d->eco_rating = (int)jlong(o, "eco_rating", -1);
    d->security = vu_text_is_security(d->description);
    a = cJSON_GetObjectItemCaseSensitive(o, "prereq");
    if (cJSON_IsArray(a)) {
        cJSON_ArrayForEach(it, a)
        {
            if (d->nprereq < VU_MAXPREREQ && cJSON_IsObject(it)) {
                vu_prereq *p = &d->prereq[d->nprereq++];
                scopy(p->kitname, sizeof p->kitname, jstr(it, "name"));
                p->package_number = jlong(it, "package_number", -1);
                p->order = (int)jlong(it, "installation_order", 0);
            }
        }
    }
    a = cJSON_GetObjectItemCaseSensitive(o, "files");
    if (cJSON_IsArray(a)) {
        cJSON_ArrayForEach(it, a)
        {
            if (d->nfiles < VU_MAXFILES && cJSON_IsObject(it)) {
                vu_file *f = &d->files[d->nfiles++];
                scopy(f->filename, sizeof f->filename, jstr(it, "filename"));
                scopy(f->save_as, sizeof f->save_as, jstr(it, "save_as"));
                f->size = jlong(it, "download_size", -1);
                scopy(f->md5, sizeof f->md5, jstr(it, "md5"));
                f->file_number = jlong(it, "package_file_number", -1);
            }
        }
    }
    cJSON_Delete(root);
    return 0;
}

void vu_entry_set_minos(vu_entry *e, const vu_detail *d)
{
    int i;
    if (d->entry.nminos <= 0)
        return;
    e->nminos = d->entry.nminos;
    for (i = 0; i < d->entry.nminos; i++)
        memcpy(e->minos[i], d->entry.minos[i], sizeof e->minos[i]);
}
