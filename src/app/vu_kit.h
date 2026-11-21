/* vu_kit.h - downloaded kit files: MD5 check and ZIP extraction (zlib inflate).
 * VUPDATE carries its own extractor because neither node has a supported unzip
 * (the Alpha has none, x86 only SYS$COMMON:[SYSHLP.UNSUPPORTED.UNZIP]); D16. */
#ifndef VU_KIT_H
#define VU_KIT_H

#include <stddef.h>
#include <stdio.h>

/* MD5 of a file as 32 lower-case hex digits. Returns 0, -1 when unreadable. */
int vu_md5_file(const char *path, char hex[33]);

/* Opens the output for one member; name is the member's base name (directories
 * dropped, already checked: no "..", no device or directory syntax). Return NULL
 * to fail the extraction. */
typedef FILE *(*vu_open_out)(const char *name, void *user);

/* Extract every file member of zippath through op. Stored and deflated members;
 * CRC-32 checked. Returns the number of files extracted, or -1 (err says why). */
int vu_unzip(const char *zippath, vu_open_out op, void *user, char *err, size_t errsz);

#endif
