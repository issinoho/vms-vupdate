/* vu_report.h - text of the CHECK report. No I/O: lines go into caller buffers. */
#ifndef VU_REPORT_H
#define VU_REPORT_H

#include "vu_plan.h"

#define VU_FL_SECURITY 1 /* the kit's description reads as a security fix (D10) */
#define VU_FL_REBOOT 2   /* restart_required */
#define VU_FL_DETAIL 4   /* flags come from the package detail (else catalogue text only) */

/* Section heading for a finding kind, e.g. "Patches". */
const char *vu_report_heading(vu_find_t k);

/* 1 for findings that are something to do (OS upgrade, patch, update). */
int vu_finding_actionable(vu_find_t k);

/* One report line (no newline), 65 columns before the flags, no trailing blanks:
 *   "  COBOL                V3.3-2             -> V3.3-3"
 *   "  VMS842L2A_SYS        (none)             -> V5.0               security, reboot"
 * CURRENT and NOTINCAT lines show only the installed version. Field-test target
 * versions are marked "field test". */
void vu_report_line(const vu_finding *f, int flags, char *buf, size_t sz);

#endif
