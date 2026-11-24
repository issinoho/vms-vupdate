/* vu_plan.h - compare an inventory with the catalogue. No I/O. */
#ifndef VU_PLAN_H
#define VU_PLAN_H

#include "vu_cat.h"
#include "vu_pcsi.h"

typedef enum {
    VU_F_UPDATE,     /* newer kit for an installed product */
    VU_F_PATCH,      /* patch for this OS version, not installed (or newer than installed) */
    VU_F_OSUPGRADE,  /* newer OpenVMS kit: reported, never installed by VUPDATE */
    VU_F_CURRENT,    /* installed product is up to date */
    VU_F_NOTINCAT    /* installed VSI product with no catalogue entry */
} vu_find_t;

typedef struct {
    vu_find_t kind;
    const vu_product *installed; /* NULL for a patch never installed */
    const vu_entry *entry;       /* NULL for CURRENT when none matched, and NOTINCAT */
} vu_finding;

typedef struct {
    vu_finding *f;
    size_t n, cap;
} vu_plan;

#define VU_OPT_FIELD_TEST 1 /* also offer non-release (A/X/T/E/B) kits */

/* Producers whose products VUPDATE matches against the VSI catalogue. */
int vu_producer_is_vsi(const char *producer);

/* 1 for an OpenVMS patch family name such as VMS842L2A_SYS or VMS923X_UPDATE.
 * Other patch kits (ECOs of layered products) are offered only when installed. */
int vu_is_os_patch_family(const char *name);

/* The OpenVMS version an OS patch family was made for: VMS923X_UPDATE -> V9.2-3,
 * VMS842L2A_SYS -> V8.4-2L2. Returns 1 when the name holds one. */
int vu_patch_family_os(const char *name, char *buf, size_t sz);

/* Catalogue rows give only the first minos of a patch, so one listed first for an
 * earlier update (V9.2-2) may also cover os_ver (V9.2-3); not one whose family
 * code names an earlier release (D26). 1 when the caller should
 * fetch the detail and merge its minos list (vu_entry_set_minos) before planning. */
int vu_patch_needs_detail(const vu_entry *e, const char *os_ver);

/* Is entry e applicable to a node running os_ver (e.g. "V8.4-2L2") on arch?
 * Patches need os_ver listed in minos, or a family code naming it, and never apply
 * when their family code names an earlier release (D26); products need os_ver >= the
 * lowest minos;
 * OS kits and kits with no minos are always applicable. */
int vu_entry_applies(const vu_entry *e, const char *os_ver, int arch);

/* The OpenVMS version to name when downloading e on a node running os_ver: os_ver
 * when e lists it, else the newest version e lists below it (V9.2-3 for a V9.2-3
 * kit on E9.2-4: the portal refuses a version the kit doesn't list, HTTP 409),
 * else os_ver. Returns os_ver or a pointer into e->minos. */
const char *vu_entry_os_for(const vu_entry *e, const char *os_ver);

/* Build the plan. os_ver is the running version (the OPENVMS/VMS product version or
 * F$GETSYI("VERSION") with its letter). For each product only the newest
 * applicable candidate is reported; for each patch family (the decoded product
 * name, e.g. VMS842L2A_SYS) only the newest applicable kit. Findings are sorted:
 * OS upgrades, patches, updates, then CURRENT and NOTINCAT, each by name.
 * Returns 0, or -2 when out of memory. */
int vu_plan_build(const vu_inventory *inv, const vu_catalog *cat, const char *os_ver,
                  int arch, int opts, vu_plan *plan);
void vu_plan_free(vu_plan *plan);

/* Is the prerequisite kit (portal kit name, e.g. "VMS842L2A_NOTARY-V0200") already
 * installed: the product present at that version or newer? 1 yes, 0 no. */
int vu_prereq_installed(const vu_inventory *inv, const char *kitname);

/* Install order for a set of kits with prerequisites (from details).
 * items[i] lists kitnames; prereq_of(i) are given as parallel arrays: for item i,
 * prereq[i][0..nprereq[i]-1] are kit names that must be installed first (only those
 * that are themselves in items count). order receives item indices, prerequisites
 * first; ties keep the input order. Returns 0, or -1 on a cycle (order then holds
 * the input order). */
int vu_install_order(size_t n, const char *const *items, const int *nprereq,
                     const char *const *const *prereq, size_t *order);

#endif
