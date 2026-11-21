/* vucheck.c - developer driver: print the plan for a saved PRODUCT SHOW PRODUCT
 * listing and a saved catalogue reply.
 *   vucheck <pss.txt> <catalogue.json> <os-version> <X86|AXP|I64> [field]
 * Not shipped; the real CHECK command comes in phase 1. */
#include "vu_cat.h"
#include "vu_pcsi.h"
#include "vu_plan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "r");
    char *buf;
    long n;
    size_t r;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    r = buf ? fread(buf, 1, (size_t)n, f) : 0;
    if (buf)
        buf[r] = '\0';
    fclose(f);
    return buf;
}

static const char *kind(vu_find_t k)
{
    switch (k) {
    case VU_F_OSUPGRADE: return "OS-UPGRADE";
    case VU_F_PATCH: return "PATCH";
    case VU_F_UPDATE: return "UPDATE";
    case VU_F_CURRENT: return "current";
    default: return "not-in-catalogue";
    }
}

int main(int argc, char **argv)
{
    vu_inventory inv;
    vu_catalog cat;
    vu_plan pl;
    char err[512];
    char *a, *b;
    size_t i;
    int arch;
    if (argc < 5) {
        fprintf(stderr, "usage: vucheck pss.txt cat.json os-version X86|AXP|I64 [field]\n");
        return 2;
    }
    a = slurp(argv[1]);
    b = slurp(argv[2]);
    if (!a || !b) {
        fprintf(stderr, "vucheck: cannot read input\n");
        return 2;
    }
    if (vu_pcsi_parse(a, &inv) != 0) {
        fprintf(stderr, "vucheck: no PRODUCT SHOW PRODUCT table in %s\n", argv[1]);
        return 2;
    }
    if (vu_catalog_parse(b, &cat, err, sizeof err) != 0) {
        fprintf(stderr, "vucheck: catalogue: %s\n", err);
        return 2;
    }
    arch = vu_arch_from_name(strcmp(argv[4], "AXP") == 0 ? "Alpha" : argv[4]);
    {
        size_t nd = 0;
        for (i = 0; i < cat.n; i++)
            if (vu_patch_needs_detail(&cat.e[i], argv[3]))
                nd++;
        printf("(%lu catalogue rows; %lu patch details to fetch before planning)\n",
               (unsigned long)cat.n, (unsigned long)nd);
    }
    if (vu_plan_build(&inv, &cat, argv[3], arch, argc > 5 ? VU_OPT_FIELD_TEST : 0, &pl) != 0)
        return 2;
    for (i = 0; i < pl.n; i++) {
        const vu_finding *f = &pl.f[i];
        printf("%-17s %-20s %-18s -> %-18s %s\n", kind(f->kind),
               f->installed ? f->installed->name : f->entry->product,
               f->installed ? f->installed->ver : "-",
               f->entry && f->kind != VU_F_CURRENT ? f->entry->ver : "",
               f->entry && f->kind != VU_F_CURRENT ? f->entry->kitname : "");
    }
    vu_plan_free(&pl);
    vu_catalog_free(&cat);
    vu_inventory_free(&inv);
    free(a);
    free(b);
    return 0;
}
