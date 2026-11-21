/* test_core.c - unit tests for src/core, run on Linux (make test) and on the
 * nodes (@build_vms TEST). Fixtures are read from tests/data/; files in
 * tests/data/real/ (git-ignored: real portal replies) add checks when present. */
#include "vu_auth.h"
#include "vu_cat.h"
#include "vu_pcsi.h"
#include "vu_plan.h"
#include "vu_report.h"
#include "vu_ver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;

#define CHECK(c)                                                                         \
    do {                                                                                 \
        checks++;                                                                        \
        if (!(c)) {                                                                      \
            fails++;                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
        }                                                                                \
    } while (0)

#define CHECK_STR(a, b)                                                                  \
    do {                                                                                 \
        checks++;                                                                        \
        if (strcmp((a), (b)) != 0) {                                                     \
            fails++;                                                                     \
            printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b));      \
        }                                                                                \
    } while (0)

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    char *buf;
    size_t n = 0, cap = 65536, r;
    if (!f)
        return NULL;
    buf = (char *)malloc(cap + 1);
    while (buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if (n == cap) {
            char *nb = (char *)realloc(buf, cap * 2 + 1);
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

static char *need(const char *path)
{
    char *s = slurp(path);
    if (!s) {
        printf("FAIL cannot read %s\n", path);
        fails++;
    }
    return s;
}

static void test_ver(void)
{
    CHECK(vu_ver_cmp("V9.2", "V9.2-0") == 0);
    CHECK(vu_ver_cmp("V13.0-3", "V13.0-03") == 0);
    CHECK(vu_ver_cmp("V2.4-62", "V2.4-68") < 0);
    CHECK(vu_ver_cmp("V5.3-5H", "V5.3-5H-1") < 0);
    CHECK(vu_ver_cmp("V9.0-28", "V9.0-28D") < 0);
    CHECK(vu_ver_cmp("V10.1-3U1", "V10.1-4") < 0);
    CHECK(vu_ver_cmp("A3.10-0RELEASE008", "V3.10-10") < 0);
    CHECK(vu_ver_cmp("V8.4-2L2", "V8.4-2L1") > 0);
    CHECK(vu_ver_cmp("E9.2-4", "V9.2-3") > 0);
    CHECK(vu_ver_cmp("V10.0", "V9.0") > 0);
    CHECK(vu_ver_cmp("V2.8-0E1", "V2.8-1") < 0);
    CHECK(vu_ver_cmp("A10.1-3_250403", "A10.1-3_250403") == 0);
    CHECK(vu_ver_is_release("V1.0"));
    CHECK(!vu_ver_is_release("A10.1-3"));
    CHECK(!vu_ver_is_release("E9.2-4"));
    CHECK(!vu_ver_is_release("T0.2-2"));
}

static void dec(const char *kit, int rc, const char *prod, const char *ver)
{
    char p[64], v[64];
    int r = vu_kitname_decode(kit, p, sizeof p, v, sizeof v);
    checks++;
    if (r != rc || strcmp(p, prod) != 0 || strcmp(v, ver) != 0) {
        fails++;
        printf("FAIL decode %s -> %d \"%s\" \"%s\" (want %d \"%s\" \"%s\")\n", kit, r, p, v, rc,
               prod, ver);
    }
}

static void test_decode(void)
{
    dec("VSI-X86VMS-X86ASM-A1001-3_250403-1", 1, "X86ASM", "A10.1-3_250403");
    dec("X86VMS-GIT-V2.44-1B-1", 1, "GIT", "V2.44-1B");
    dec("VMS842L2A_SYS-V0500", 1, "VMS842L2A_SYS", "V5.0");
    dec("VMS842L2A_RTL-V1000", 1, "VMS842L2A_RTL", "V10.0");
    dec("X86VMS-PERFMGMT-V0301-1-ECO1", 1, "PERFMGMT", "V3.1-1ECO1");
    dec("X86VMS-ANT-V0110-14A-1", 1, "ANT", "V1.10-14A");
    dec("VSI-X86VMS-VMSI18N-V0902--1", 1, "VMSI18N", "V9.2");
    dec("X86VMS-OPENVMS-V0902-3", 1, "OPENVMS", "V9.2-3");
    dec("X86VMS-OPENVMS-E0902-4", 1, "OPENVMS", "E9.2-4");
    dec("AXPVMS-OPENVMS-V0804-2L2", 1, "OPENVMS", "V8.4-2L2");
    dec("X86VMS-ENTERPRISE-DIRECTORY-V0509-1", 1, "ENTERPRISE-DIRECTORY", "V5.9-1");
    dec("VSI-X86VMS-STUNNEL-V0556-0BA-1", 1, "STUNNEL", "V5.56-0BA");
    dec("X86VMS_CONSOLEMGR-V0301", 1, "CONSOLEMGR", "V3.1");
    dec("VMS923X_TIMEZONE-V0100-4", 1, "VMS923X_TIMEZONE", "V1.0"); /* -4: PCSI kit type */
    dec("VMS842L2A_TIMEZONE-V0100-4", 1, "VMS842L2A_TIMEZONE", "V1.0");
    dec("AXPVMS-OPENSSH-V0909A-2-1", 2, "OPENSSH", "");
    dec("AMBASSADOR_26_X86", 0, "AMBASSADOR_26_X86", "");
    dec("VSI$SUPPORT", 0, "VSI$SUPPORT", "");
}

static void test_pcsi(void)
{
    vu_inventory inv;
    const vu_product *p;
    char *t = need("tests/data/pss_axp.txt");
    if (!t)
        return;
    CHECK(vu_pcsi_parse(t, &inv) == 0);
    p = vu_inventory_find(&inv, "OPENSSH");
    CHECK(p != NULL);
    if (p) {
        CHECK_STR(p->producer, "VSI");
        CHECK_STR(p->base, "AXPVMS");
        CHECK_STR(p->ver, "V9.9-2A");
        CHECK(p->kit == VU_KIT_FULL);
    }
    p = vu_inventory_find(&inv, "OPENVMS");
    CHECK(p && p->kit == VU_KIT_PLATFORM && strcmp(p->ver, "V8.4-2L2") == 0);
    p = vu_inventory_find(&inv, "VMS");
    CHECK(p && p->kit == VU_KIT_OS);
    /* Applied patches come from the MAINTENANCE column. */
    p = vu_inventory_find(&inv, "VMS842L2A_RTL");
    CHECK(p && p->kit == VU_KIT_PATCH && p->maintenance && strcmp(p->ver, "V10.0") == 0);
    CHECK(vu_inventory_find(&inv, "VMS842L2A_NOTARY") != NULL);
    CHECK(vu_inventory_find(&inv, "VMS842L2A_DPML") != NULL);
    CHECK(vu_inventory_find(&inv, "VMS842L2A_SYS") == NULL);
    CHECK(inv.n == 33 + 3);
    vu_inventory_free(&inv);
    free(t);

    t = need("tests/data/pss_x86.txt");
    if (!t)
        return;
    CHECK(vu_pcsi_parse(t, &inv) == 0);
    p = vu_inventory_find(&inv, "X86ASM");
    CHECK(p && strcmp(p->ver, "A10.1-3_250403") == 0);
    p = vu_inventory_find(&inv, "OPENVMS");
    CHECK(p && strcmp(p->ver, "E9.2-4") == 0);
    vu_inventory_free(&inv);
    free(t);

    CHECK(vu_pcsi_parse("no table here\n", &inv) == -1);
    vu_inventory_free(&inv);
}

static void test_cat(void)
{
    vu_catalog cat;
    vu_detail d;
    char err[256];
    size_t i;
    int found = 0;
    char *t = need("tests/data/cat_axp.json");
    if (!t)
        return;
    CHECK(vu_catalog_parse(t, &cat, err, sizeof err) == 0);
    CHECK(cat.n == 26);
    for (i = 0; i < cat.n; i++) {
        const vu_entry *e = &cat.e[i];
        if (strcmp(e->kitname, "VMS842L2A_SYS-V0500") == 0) {
            found = 1;
            CHECK(e->type == 'E');
            CHECK(e->arch == VU_ARCH_AXP);
            CHECK(e->nminos == 1 && strcmp(e->minos[0], "V8.4-2L2") == 0);
            CHECK(e->package_number == 1814);
            CHECK_STR(e->product, "VMS842L2A_SYS");
            CHECK_STR(e->ver, "V5.0");
        }
        if (strcmp(e->kitname, "AXPVMS-OPENSSH-V0909A-2-1") == 0)
            CHECK_STR(e->ver, "V9.9-2A"); /* from the portal version field */
    }
    CHECK(found);
    vu_catalog_free(&cat);
    free(t);

    CHECK(vu_catalog_parse("{\"reason\":\"'sysarch' is a required property\",\"success\":false}",
                           &cat, err, sizeof err) == -3);
    CHECK(strstr(err, "sysarch") != NULL);
    CHECK(vu_catalog_parse("<html>", &cat, err, sizeof err) == -1);

    t = need("tests/data/det_update.json");
    if (t) {
        CHECK(vu_detail_parse(t, &d, err, sizeof err) == 0);
        CHECK_STR(d.entry.kitname, "VMS923X_UPDATE-V0300");
        CHECK(d.restart_required == 1);
        CHECK(d.nprereq == 1 && strcmp(d.prereq[0].kitname, "VMS923X_PCSI-V0100") == 0);
        CHECK(d.prereq[0].order == 1 && d.prereq[0].package_number == 1502);
        CHECK(d.nfiles == 1 && d.files[0].size == 52252372 && d.files[0].md5[0] != '\0');
        CHECK(d.entry.nminos == 1);
        CHECK(!d.security);
        free(t);
    }
    t = need("tests/data/det_security.json");
    if (t) {
        CHECK(vu_detail_parse(t, &d, err, sizeof err) == 0);
        CHECK(d.security);
        CHECK(d.restart_required == 0);
        CHECK(d.nprereq == 2 && d.prereq[1].order == 2);
        CHECK(d.entry.nminos == 2);
        free(t);
    }
}

static const vu_finding *find(const vu_plan *pl, vu_find_t k, const char *name)
{
    size_t i;
    for (i = 0; i < pl->n; i++) {
        const vu_finding *f = &pl->f[i];
        const char *n = f->installed ? f->installed->name : f->entry->product;
        if (f->kind == k && strcmp(n, name) == 0)
            return f;
    }
    return NULL;
}

static void test_plan_axp(void)
{
    vu_inventory inv;
    vu_catalog cat;
    vu_plan pl;
    const vu_finding *f;
    char *a = need("tests/data/pss_axp.txt"), *b = need("tests/data/cat_axp.json");
    if (!a || !b)
        return;
    vu_pcsi_parse(a, &inv);
    vu_catalog_parse(b, &cat, NULL, 0);
    CHECK(vu_plan_build(&inv, &cat, "V8.4-2L2", VU_ARCH_AXP, 0, &pl) == 0);

    /* SYS: V5.0 offered, V4.0 superseded; L1 kits never. */
    f = find(&pl, VU_F_PATCH, "VMS842L2A_SYS");
    CHECK(f && strcmp(f->entry->ver, "V5.0") == 0 && f->installed == NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS842L1A_SYS") == NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS842L2A_BACKUP") != NULL);
    /* Installed patches at the newest level are not offered. */
    CHECK(find(&pl, VU_F_PATCH, "VMS842L2A_RTL") == NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS842L2A_DPML") == NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS842L2A_NOTARY") == NULL);
    /* Installed SSL3 V3.0-22 is the newest. CURL V8.0-1A -> V8.13. */
    CHECK(find(&pl, VU_F_CURRENT, "SSL3") != NULL);
    f = find(&pl, VU_F_UPDATE, "CURL");
    CHECK(f && strcmp(f->entry->ver, "V8.13-0") == 0);
    /* OPENSSH V9.9-2A installed; V9.9-2B and V9.9-2C exist. */
    f = find(&pl, VU_F_UPDATE, "OPENSSH");
    CHECK(f && strcmp(f->entry->ver, "V9.9-2C") == 0);
    /* OS: V8.4-2L2 is the newest Alpha release. */
    CHECK(find(&pl, VU_F_OSUPGRADE, "OPENVMS") == NULL);
    CHECK(find(&pl, VU_F_CURRENT, "OPENVMS") != NULL);
    /* Not VSI: ignored. VSI but not in the catalogue: listed. */
    CHECK(find(&pl, VU_F_NOTINCAT, "BESZEL") == NULL);
    CHECK(find(&pl, VU_F_NOTINCAT, "DECSET") != NULL);
    /* Ordering: patches before updates before current. */
    CHECK(pl.n > 2 && pl.f[0].kind == VU_F_PATCH);
    vu_plan_free(&pl);
    vu_catalog_free(&cat);
    vu_inventory_free(&inv);
    free(a);
    free(b);
}

static void test_plan_x86(void)
{
    vu_inventory inv;
    vu_catalog cat;
    vu_plan pl;
    const vu_finding *f;
    char *a = need("tests/data/pss_x86.txt"), *b = need("tests/data/cat_x86.json");
    if (!a || !b)
        return;
    vu_pcsi_parse(a, &inv);
    vu_catalog_parse(b, &cat, NULL, 0);
    CHECK(vu_plan_build(&inv, &cat, "E9.2-4", VU_ARCH_X86, 0, &pl) == 0);
    /* V9.2-3 patches don't apply to E9.2-4. */
    CHECK(find(&pl, VU_F_PATCH, "VMS923X_UPDATE") == NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS923X_DCL") == NULL);
    /* PERFMGMT ECO: product not installed, so not offered. */
    CHECK(find(&pl, VU_F_PATCH, "PERFMGMT") == NULL);
    f = find(&pl, VU_F_UPDATE, "APACHEWS");
    CHECK(f && strcmp(f->entry->ver, "V2.4-68") == 0);
    /* LUA V5.3-5H: the portal's "V5.3-5H-1" is the kit revision, not newer. */
    CHECK(find(&pl, VU_F_CURRENT, "LUA") != NULL);
    /* Name decoding makes X86ASM A10.1-3_250403 equal; V10.1-4 is the update. */
    f = find(&pl, VU_F_UPDATE, "X86ASM");
    CHECK(f && strcmp(f->entry->ver, "V10.1-4") == 0);
    /* CXX: release V10.1-4 offered, field-test A10.1-3_250618 not. */
    f = find(&pl, VU_F_UPDATE, "CXX");
    CHECK(f && strcmp(f->entry->ver, "V10.1-4") == 0);
    CHECK(find(&pl, VU_F_CURRENT, "VMSI18N") != NULL);
    CHECK(find(&pl, VU_F_CURRENT, "OPENSSH") != NULL);
    /* E9.2-4 node: the E9.2-4 kit is the one installed. */
    CHECK(find(&pl, VU_F_OSUPGRADE, "OPENVMS") == NULL);
    vu_plan_free(&pl);

    /* Same inventory on V9.2-3: the patches and the OS kit apply. */
    CHECK(vu_plan_build(&inv, &cat, "V9.2-3", VU_ARCH_X86, 0, &pl) == 0);
    CHECK(find(&pl, VU_F_PATCH, "VMS923X_UPDATE") != NULL);
    CHECK(find(&pl, VU_F_PATCH, "VMS923X_DCL") != NULL);
    vu_plan_free(&pl);
    {
        /* Detail merge: with the full minos list the V9.2-3 UPDATE kit stays off a
         * V9.2-2 node, and DCL V4.0 (minos V9.2-2, V9.2-3) is on it. */
        char *dj = need("tests/data/det_security.json");
        vu_detail d;
        size_t k;
        if (dj && vu_detail_parse(dj, &d, NULL, 0) == 0) {
            for (k = 0; k < cat.n; k++)
                if (cat.e[k].package_number == d.entry.package_number)
                    vu_entry_set_minos(&cat.e[k], &d);
            CHECK(vu_plan_build(&inv, &cat, "V9.2-3", VU_ARCH_X86, 0, &pl) == 0);
            CHECK(find(&pl, VU_F_PATCH, "VMS923X_DCL") != NULL);
            vu_plan_free(&pl);
        }
        free(dj);
    }
    vu_catalog_free(&cat);
    vu_inventory_free(&inv);
    free(a);
    free(b);
}

static void test_applies(void)
{
    vu_entry e;
    memset(&e, 0, sizeof e);
    e.type = 'O';
    e.arch = VU_ARCH_X86;
    e.nminos = 1;
    strcpy(e.minos[0], "V9.2-3");
    CHECK(vu_entry_applies(&e, "V9.2-3", VU_ARCH_X86));
    CHECK(vu_entry_applies(&e, "E9.2-4", VU_ARCH_X86));
    CHECK(!vu_entry_applies(&e, "V9.2-2", VU_ARCH_X86));
    CHECK(!vu_entry_applies(&e, "V9.2-3", VU_ARCH_AXP));
    strcpy(e.minos[0], "windows");
    CHECK(!vu_entry_applies(&e, "V9.2-3", VU_ARCH_X86));
    CHECK(vu_is_os_patch_family("VMS842L2A_SYS"));
    CHECK(vu_is_os_patch_family("VMS923X_UPDATE"));
    CHECK(!vu_is_os_patch_family("PERFMGMT"));
    CHECK(!vu_is_os_patch_family("VMSI18N"));
    {
        char b[16];
        CHECK(vu_patch_family_os("VMS923X_UPDATE", b, sizeof b) && strcmp(b, "V9.2-3") == 0);
        CHECK(vu_patch_family_os("VMS842L2A_SYS", b, sizeof b) && strcmp(b, "V8.4-2L2") == 0);
        CHECK(!vu_patch_family_os("PERFMGMT", b, sizeof b));
    }
    /* A V9.2-2 patch may also cover V9.2-3: fetch its detail. Not V9.1 kits on 9.2. */
    e.type = 'E';
    strcpy(e.minos[0], "V9.2-2");
    strcpy(e.product, "VMS922X_SORT");
    CHECK(vu_patch_needs_detail(&e, "V9.2-3"));
    CHECK(!vu_patch_needs_detail(&e, "V9.2-2"));
    strcpy(e.minos[0], "V9.1");
    CHECK(!vu_patch_needs_detail(&e, "V9.2-3"));
    strcpy(e.minos[0], "V8.4-2L1");
    strcpy(e.product, "VMS842L1A_SYS");
    CHECK(vu_patch_needs_detail(&e, "V8.4-2L2"));
    CHECK(!vu_entry_applies(&e, "V8.4-2L2", 0));
}

static void test_order(void)
{
    const char *items[] = { "UPDATE-V0300", "DCL-V0400", "PCSI-V0100", "UPDATE-V0200" };
    const char *p0[] = { "PCSI-V0100" };
    const char *p1[] = { "UPDATE-V0200", "PCSI-V0100" };
    const char *const *pre[4];
    int np[4] = { 1, 2, 0, 0 };
    size_t order[4];
    const char *cyc_items[] = { "A", "B" };
    const char *ca[] = { "B" }, *cb[] = { "A" };
    const char *const *cpre[2];
    int cnp[2] = { 1, 1 };
    pre[0] = p0;
    pre[1] = p1;
    pre[2] = NULL;
    pre[3] = NULL;
    CHECK(vu_install_order(4, items, np, pre, order) == 0);
    /* PCSI first, then UPDATE-V0300 (its only prereq done), then UPDATE-V0200, DCL. */
    CHECK(order[0] == 2 && order[1] == 0 && order[2] == 3 && order[3] == 1);
    cpre[0] = ca;
    cpre[1] = cb;
    CHECK(vu_install_order(2, cyc_items, cnp, cpre, order) == -1);
    CHECK(order[0] == 0 && order[1] == 1);
}

static void test_auth(void)
{
    char b[256];
    vu_token t;
    CHECK(vu_form_escape("a.b-c_d*", b, sizeof b) == 8);
    CHECK_STR(b, "a.b-c_d*");
    CHECK(vu_form_escape("user@example.com", b, sizeof b) > 0);
    CHECK_STR(b, "user%40example.com");
    CHECK(vu_form_escape("p&w=d +%", b, sizeof b) > 0);
    CHECK_STR(b, "p%26w%3Dd%20%2B%25");
    CHECK(vu_form_escape("\xc3\xa9", b, sizeof b) > 0); /* UTF-8 e-acute */
    CHECK_STR(b, "%C3%A9");
    CHECK(vu_form_escape("abcd", b, 4) == -1);
    CHECK(vu_form_escape("abc", b, 4) == 3);
    CHECK(vu_token_body("u@x.org", "a&b", b, sizeof b) > 0);
    CHECK_STR(b, "client_id=sp&grant_type=password&username=u%40x.org&password=a%26b");
    CHECK(vu_token_body("u@x.org", "a&b", b, 40) == -1);

    CHECK(vu_token_parse("{\"access_token\":\"eyJ.x.y\",\"expires_in\":180,"
                         "\"token_type\":\"Bearer\"}", &t) == 0);
    CHECK_STR(t.access, "eyJ.x.y");
    CHECK(t.expires_in == 180);
    CHECK(vu_token_parse("{\"error\":\"invalid_grant\",\"error_description\":"
                         "\"Invalid user credentials\"}", &t) == -3);
    CHECK_STR(t.error, "Invalid user credentials");
    CHECK(t.access[0] == '\0');
    CHECK(vu_token_parse("{\"error\":\"unauthorized_client\"}", &t) == -3);
    CHECK_STR(t.error, "unauthorized_client");
    CHECK(vu_token_parse("<html>bad gateway</html>", &t) == -1);
    CHECK(vu_token_parse("{\"x\":1}", &t) == -1);
}

static void test_kitfiles(void)
{
    char pr[32], ba[16], pd[48];
    vu_inventory inv;
    char *a = need("tests/data/pss_axp.txt");
    CHECK(vu_kitfile_kind("VSI-AXPVMS-VMS842L2A_SYS-V0500--4.PCSI$COMPRESSED") == VU_KF_COMPRESSED);
    CHECK(vu_kitfile_kind("vsi-axpvms-vms842l2a_sys-v0500--4.pcsi$compressed_esw;1") == VU_KF_MANIFEST);
    CHECK(vu_kitfile_kind("VSI-X86VMS-COBOL-V0305-02-1.PCSI") == VU_KF_PCSI);
    CHECK(vu_kitfile_kind("VSI-X86VMS-COBOL-V0305-02-1.PCSI_ESW") == VU_KF_MANIFEST);
    CHECK(vu_kitfile_kind("VSI-AXPVMS-VMS842L2A_TIMEZONE-V0100--4.PCSI$COMPRESSED_VNC") == VU_KF_MANIFEST);
    CHECK(vu_kitfile_kind("README.TXT") == VU_KF_NONE);
    CHECK(vu_kitfile_kind("COBOL_RELEASE_NOTES.PDF") == VU_KF_NONE);
    CHECK(vu_kitfile_parse("VSI-AXPVMS-VMS842L2A_SYS-V0500--4.PCSI$COMPRESSED", pr, sizeof pr,
                           ba, sizeof ba, pd, sizeof pd) == 1);
    CHECK_STR(pr, "VSI");
    CHECK_STR(ba, "AXPVMS");
    CHECK_STR(pd, "VMS842L2A_SYS");
    CHECK(vu_kitfile_parse("VSI-X86VMS-COBOL-V0305-02-1.PCSI$COMPRESSED;1", pr, sizeof pr, ba,
                           sizeof ba, pd, sizeof pd) == 1);
    CHECK_STR(pd, "COBOL");
    CHECK(vu_kitfile_parse("NOTAKIT.PCSI", pr, sizeof pr, ba, sizeof ba, pd, sizeof pd) == 0);
    if (a && vu_pcsi_parse(a, &inv) == 0) {
        /* NOTARY V2.0 is applied on the Alpha; SYS V5.0 isn't. */
        CHECK(vu_prereq_installed(&inv, "VMS842L2A_NOTARY-V0200") == 1);
        CHECK(vu_prereq_installed(&inv, "VMS842L2A_NOTARY-V0300") == 0);
        CHECK(vu_prereq_installed(&inv, "VMS842L2A_SYS-V0500") == 0);
        CHECK(vu_prereq_installed(&inv, "AXPVMS-OPENSSH-V0909-2A-1") == 1);
        vu_inventory_free(&inv);
    }
    free(a);
}

static void test_report(void)
{
    vu_product p;
    vu_entry e;
    vu_finding f;
    char b[160];
    memset(&p, 0, sizeof p);
    memset(&e, 0, sizeof e);
    strcpy(p.name, "COBOL");
    strcpy(p.ver, "V3.3-2");
    strcpy(e.product, "COBOL");
    strcpy(e.ver, "V3.3-3");
    f.kind = VU_F_UPDATE;
    f.installed = &p;
    f.entry = &e;
    vu_report_line(&f, 0, b, sizeof b);
    CHECK_STR(b, "  COBOL                V3.3-2             -> V3.3-3");
    strcpy(e.ver, "A3.4-1");
    vu_report_line(&f, VU_FL_SECURITY, b, sizeof b);
    CHECK_STR(b, "  COBOL                V3.3-2             -> A3.4-1             field test, security");
    f.kind = VU_F_PATCH;
    f.installed = NULL;
    strcpy(e.product, "VMS842L2A_SYS");
    strcpy(e.ver, "V5.0");
    vu_report_line(&f, VU_FL_SECURITY | VU_FL_REBOOT | VU_FL_DETAIL, b, sizeof b);
    CHECK_STR(b, "  VMS842L2A_SYS        (none)             -> V5.0               security, reboot");
    f.kind = VU_F_CURRENT;
    f.installed = &p;
    vu_report_line(&f, 0, b, sizeof b);
    CHECK_STR(b, "  COBOL                V3.3-2");
    f.kind = VU_F_NOTINCAT;
    f.entry = NULL;
    vu_report_line(&f, 0, b, sizeof b);
    CHECK_STR(b, "  COBOL                V3.3-2");
    CHECK(vu_finding_actionable(VU_F_PATCH) && !vu_finding_actionable(VU_F_CURRENT));
    CHECK(strcmp(vu_report_heading(VU_F_PATCH), "Patches") == 0);
}

/* Real portal replies, when present (Linux developer runs only). */
static void test_real(void)
{
    vu_catalog cat;
    char *t = slurp("tests/data/real/cat_x86.json");
    size_t i, noprod = 0;
    if (!t)
        return;
    CHECK(vu_catalog_parse(t, &cat, NULL, 0) == 0);
    CHECK(cat.n > 100);
    for (i = 0; i < cat.n; i++)
        if (!cat.e[i].ver[0])
            noprod++;
    CHECK(noprod == 0);
    vu_catalog_free(&cat);
    free(t);
    printf("(real portal fixtures checked)\n");
}

int main(void)
{
    test_ver();
    test_decode();
    test_pcsi();
    test_cat();
    test_applies();
    test_plan_axp();
    test_plan_x86();
    test_order();
    test_auth();
    test_report();
    test_kitfiles();
    test_real();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
