/* vu_cat.h - the portal catalogue (PUT /vsp/pkgman/packages) and package detail
 * (GET /vsp/pkgman/packages/package) parsed from JSON text. No I/O. */
#ifndef VU_CAT_H
#define VU_CAT_H

#include <stddef.h>

#define VU_MAXMINOS 8
#define VU_MAXPREREQ 16
#define VU_MAXFILES 4

/* Portal "type" letters. */
#define VU_T_PATCH 'E'   /* ECO / patch kit */
#define VU_T_LP 'L'      /* layered product */
#define VU_T_OPEN 'O'    /* open source */
#define VU_T_OS 'V'      /* OpenVMS itself */
#define VU_T_LICENCE 'D' /* licence (PAK) bundle */
#define VU_T_WINDOWS 'W' /* Windows-side tool */

#define VU_ARCH_X86 1
#define VU_ARCH_I64 2
#define VU_ARCH_AXP 4

typedef struct {
    char kitname[64];   /* portal "name", e.g. X86VMS-OPENSSH-V0809-1K01-1 */
    char product[48];   /* decoded product, e.g. OPENSSH */
    char ver[48];       /* decoded from kitname, else the portal "version" */
    char portal_ver[48];/* the portal "version" field as given */
    char type;          /* VU_T_* */
    int arch;           /* VU_ARCH_* bits */
    int nminos;
    char minos[VU_MAXMINOS][16];
    long package_number;
    char release_date[16];
    int verified;
    char description[256];
} vu_entry;

typedef struct {
    vu_entry *e;
    size_t n;
} vu_catalog;

typedef struct {
    char kitname[64];
    long package_number;
    int order; /* installation_order, 0 when not given */
} vu_prereq;

typedef struct {
    char filename[96];
    char save_as[96];
    long size;
    char md5[40]; /* empty when the portal gives none */
    long file_number;
} vu_file;

typedef struct {
    vu_entry entry; /* the same fields as the catalogue row */
    int restart_required; /* 1, 0, or -1 when not given */
    int eco_rating;       /* -1 when not given */
    int security;         /* 1 when the description reads as a security fix */
    int nprereq;
    vu_prereq prereq[VU_MAXPREREQ];
    int nfiles;
    vu_file files[VU_MAXFILES];
    char description[1024];
} vu_detail;

/* Parse a catalogue reply (a JSON array). Returns 0, -1 on bad JSON/shape, -2 on
 * out of memory. On a portal error object ({"success":false,"reason":...}) returns
 * -3 and copies the reason into err (when err is not NULL). */
int vu_catalog_parse(const char *json, vu_catalog *cat, char *err, size_t errsz);
void vu_catalog_free(vu_catalog *cat);

/* Parse a package detail reply (a JSON array of one object). Same return codes. */
int vu_detail_parse(const char *json, vu_detail *d, char *err, size_t errsz);

/* Replace a catalogue row's minos (first value only) with the detail's full list. */
void vu_entry_set_minos(vu_entry *e, const vu_detail *d);

/* 1 when the text reads as a security fix: CVE-, "security", "vulnerab", "exploit". */
int vu_text_is_security(const char *text);

/* Architecture bit for this node: "X86_64", "IA64", "Alpha" (F$GETSYI ARCH_NAME). */
int vu_arch_from_name(const char *arch_name);
const char *vu_arch_portal_name(int arch); /* "X86", "I64", "AXP" */

/* Kit dependencies from a patch kit's release notes (the .TXT file): the kit
 * names under the "KIT DEPENDENCIES" heading, up to the next numbered heading.
 * The portal's prereq list can miss some (N22). Names are normalised to the
 * portal form ("VMS842L2A_NOTARY_V0200" -> "VMS842L2A_NOTARY-V0200"). Returns
 * the number found (0 when the section is absent or says None). */
int vu_notes_dependencies(const char *text, char names[][64], int max);

#endif
