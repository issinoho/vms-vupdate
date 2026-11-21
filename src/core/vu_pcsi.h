/* vu_pcsi.h - parse the text of PRODUCT SHOW PRODUCT * /FULL into an inventory.
 * Pure: takes the whole output as one string. */
#ifndef VU_PCSI_H
#define VU_PCSI_H

#include <stddef.h>

typedef enum {
    VU_KIT_FULL,     /* "Full LP" */
    VU_KIT_PATCH,    /* "Patch" */
    VU_KIT_PLATFORM, /* "Platform" */
    VU_KIT_OS,       /* "Oper System" */
    VU_KIT_OTHER     /* Partial, Transition, Mandatory... */
} vu_kit_t;

typedef struct {
    char producer[32]; /* VSI, ISSINOHO, DEC ... */
    char base[16];     /* X86VMS, AXPVMS, VMS ... */
    char name[48];     /* OPENSSH, VMS842L2A_RTL ... */
    char ver[32];      /* V9.9-2C */
    vu_kit_t kit;
    int maintenance;   /* 1: listed only in a MAINTENANCE column (an applied patch) */
} vu_product;

typedef struct {
    vu_product *p;
    size_t n, cap;
} vu_inventory;

/* Parse the output. Product rows give kit types; every entry in a MAINTENANCE
 * column is added too (marked maintenance=1, kit=PATCH) unless already present.
 * Returns 0 on success, -1 when no table header was found, -2 on out of memory. */
int vu_pcsi_parse(const char *text, vu_inventory *inv);
void vu_inventory_free(vu_inventory *inv);

/* Find an installed product by name (case-insensitive); NULL when absent. */
const vu_product *vu_inventory_find(const vu_inventory *inv, const char *name);

/* Parse "PRODUCER BASE NAME VERSION" into a product. Returns 1 on success. */
int vu_pcsi_parse_spec(const char *spec, size_t len, vu_product *out);

/* PRODUCT SHOW RECOVERY_DATA (or the same table PRODUCT INSTALL prints): the
 * number of recovery data sets, and the patch names in them (first max into
 * names). 0 when there are none (%PCSIUI-I-NORDSET). */
int vu_pcsi_parse_recovery(const char *text, char names[][48], int max, int *npatches);

/* The PCSI kit type digit in a kit file name ("...-V0500--4.PCSI$COMPRESSED" -> 4):
 * 1 full, 2 operating system, 3 partial, 4 patch, 5 platform, 6 transition,
 * 7 mandatory update; 0 when not there. */
int vu_kitfile_type(const char *filename);

/* Kit files in a downloaded ZIP. */
#define VU_KF_NONE 0
#define VU_KF_PCSI 1       /* .PCSI */
#define VU_KF_COMPRESSED 2 /* .PCSI$COMPRESSED */
#define VU_KF_MANIFEST 3   /* signature manifest: .PCSI[$COMPRESSED]_ESW or _VNC */

/* Classify a file name (any case, version ";n" ignored). */
int vu_kitfile_kind(const char *filename);

/* Split a PCSI kit file name such as "VSI-AXPVMS-VMS842L2A_SYS-V0500--4.PCSI$COMPRESSED"
 * into producer (VSI), base (AXPVMS) and product (VMS842L2A_SYS). Returns 1 on
 * success, 0 when the name isn't producer-base-product-version. */
int vu_kitfile_parse(const char *filename, char *producer, size_t prodsz, char *base,
                     size_t basesz, char *product, size_t productsz);

#endif
