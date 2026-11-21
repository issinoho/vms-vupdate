/* vu_ver.h - OpenVMS kit version strings: comparison, stage, and decoding from
 * portal kit names. Pure functions, no I/O. */
#ifndef VU_VER_H
#define VU_VER_H

#include <stddef.h>

#define VU_VER_MAX 48

/* Compare two version strings such as "V8.9-1K01", "A10.1-3_250403", "E9.2-4".
 * The leading stage letter is ignored. Numbers compare numerically (leading zeros
 * don't count), letter runs compare as strings, '.', '-' and '_' only separate.
 * Trailing zero numbers are dropped, so "V9.2" equals "V9.2-0". Where a letter run
 * meets a number at the same place the letter run is older ("3U1" < "3250618").
 * Returns <0, 0 or >0 like strcmp. */
int vu_ver_cmp(const char *a, const char *b);

/* 1 when the version is a release (stage letter V, or no letter), 0 for field test,
 * advance, experimental or early kits (A, B, E, T, X, ...). */
int vu_ver_is_release(const char *v);

/* Decode the version encoded in a portal kit name, e.g.
 *   "VSI-X86VMS-X86ASM-A1001-3_250403-1" -> "A10.1-3_250403"
 *   "X86VMS-GIT-V2.44-1B-1"              -> "V2.44-1B"
 *   "VMS842L2A_SYS-V0500"                -> "V5.0"
 *   "X86VMS-PERFMGMT-V0301-1-ECO1"       -> "V3.1-1ECO1"
 * The product part (before the version) goes to product (prefixes such as "VSI-"
 * and "X86VMS-" removed), the version to ver. When the name holds no version,
 * product is the name without prefixes, ver is empty, and 0 is returned.
 * Returns 1 when a version was found; 2 when the product was found but the version
 * can't be read from the name ("AXPVMS-OPENSSH-V0909A-2-1": use the portal's
 * version field); 0 when there is no version field. Output buffers may be NULL. */
int vu_kitname_decode(const char *kitname, char *product, size_t prodsz,
                      char *ver, size_t versz);

#endif
