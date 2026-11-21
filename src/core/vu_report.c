/* vu_report.c - see vu_report.h. */
#include "vu_report.h"
#include "vu_ver.h"

#include <stdio.h>
#include <string.h>

const char *vu_report_heading(vu_find_t k)
{
    switch (k) {
    case VU_F_OSUPGRADE: return "OpenVMS upgrades (reported only, never installed by VUPDATE)";
    case VU_F_PATCH: return "Patches";
    case VU_F_UPDATE: return "Updates";
    case VU_F_CURRENT: return "Up to date";
    default: return "Installed, not in the VSI catalogue";
    }
}

int vu_finding_actionable(vu_find_t k)
{
    return k == VU_F_OSUPGRADE || k == VU_F_PATCH || k == VU_F_UPDATE;
}

void vu_report_line(const vu_finding *f, int flags, char *buf, size_t sz)
{
    const char *name = f->installed ? f->installed->name : f->entry ? f->entry->product : "?";
    const char *have = f->installed ? f->installed->ver : "(none)";
    char tail[64];
    tail[0] = '\0';
    if (!vu_finding_actionable(f->kind) || !f->entry) {
        snprintf(buf, sz, "  %-20s %s", name, have);
        return;
    }
    if (!vu_ver_is_release(f->entry->ver))
        strcat(tail, ", field test");
    if (flags & VU_FL_SECURITY)
        strcat(tail, ", security");
    if (flags & VU_FL_REBOOT)
        strcat(tail, ", reboot");
    snprintf(buf, sz, "  %-20s %-16s -> %-16s%s%s", name, have, f->entry->ver,
             tail[0] ? " " : "", tail[0] ? tail + 2 : "");
}
