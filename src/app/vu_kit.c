/* vu_kit.c - see vu_kit.h. OpenSSL for MD5, zlib for inflate. */
#include "vu_kit.h"

#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define CHUNK 65536

int vu_md5_file(const char *path, char hex[33])
{
    static const char hx[] = "0123456789abcdef";
    FILE *f = fopen(path, "rb");
    unsigned char *buf, md[EVP_MAX_MD_SIZE];
    unsigned int mdlen = 0, i;
    size_t n;
    int ok;
    EVP_MD_CTX *c;
    if (!f)
        return -1;
    buf = (unsigned char *)malloc(CHUNK);
    c = EVP_MD_CTX_new();
    ok = buf && c && EVP_DigestInit_ex(c, EVP_md5(), NULL);
    while (ok && (n = fread(buf, 1, CHUNK, f)) > 0)
        ok = EVP_DigestUpdate(c, buf, n);
    ok = ok && !ferror(f) && EVP_DigestFinal_ex(c, md, &mdlen) && mdlen == 16;
    EVP_MD_CTX_free(c);
    free(buf);
    fclose(f);
    if (!ok)
        return -1;
    for (i = 0; i < 16; i++) {
        hex[2 * i] = hx[md[i] >> 4];
        hex[2 * i + 1] = hx[md[i] & 15];
    }
    hex[32] = '\0';
    return 0;
}

static unsigned long get16(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8;
}

static unsigned long get32(const unsigned char *p)
{
    return get16(p) | get16(p + 2) << 16;
}

static void seterr(char *err, size_t sz, const char *what, const char *name)
{
    if (err && sz)
        snprintf(err, sz, "%s%s%s", what, name ? ": " : "", name ? name : "");
}

/* A safe base name: last path component; NULL when unusable. */
static const char *base_name(const char *name)
{
    const char *b = strrchr(name, '/');
    b = b ? b + 1 : name;
    if (!*b || strcmp(b, ".") == 0 || strcmp(b, "..") == 0 || strpbrk(b, ":[]<>\\") != NULL)
        return NULL;
    return b;
}

/* Copy (stored) or inflate (deflated) csize bytes from in to out; check the CRC. */
static int copy_member(FILE *in, FILE *out, int method, unsigned long csize,
                       unsigned long usize, unsigned long crc_want)
{
    unsigned char *ib = (unsigned char *)malloc(CHUNK), *ob = (unsigned char *)malloc(CHUNK);
    unsigned long crc = crc32(0L, Z_NULL, 0), left = csize, total = 0;
    z_stream z;
    int rc = -1, zr = Z_OK;
    memset(&z, 0, sizeof z);
    if (!ib || !ob || (method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK))
        goto done;
    while (left > 0) {
        size_t want = left < CHUNK ? (size_t)left : CHUNK;
        size_t got = fread(ib, 1, want, in);
        if (got == 0)
            goto done_z;
        left -= (unsigned long)got;
        if (method == 0) {
            if (fwrite(ib, 1, got, out) != got)
                goto done_z;
            crc = crc32(crc, ib, (uInt)got);
            total += (unsigned long)got;
            continue;
        }
        z.next_in = ib;
        z.avail_in = (uInt)got;
        do {
            size_t have;
            z.next_out = ob;
            z.avail_out = CHUNK;
            zr = inflate(&z, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END)
                goto done_z;
            have = CHUNK - z.avail_out;
            if (have && fwrite(ob, 1, have, out) != have)
                goto done_z;
            crc = crc32(crc, ob, (uInt)have);
            total += (unsigned long)have;
        } while (z.avail_out == 0 && zr != Z_STREAM_END);
        if (zr == Z_STREAM_END)
            break;
    }
    if ((method == 0 || zr == Z_STREAM_END) && total == usize && crc == crc_want)
        rc = 0;
done_z:
    if (method == 8)
        inflateEnd(&z);
done:
    free(ib);
    free(ob);
    return rc;
}

int vu_unzip(const char *zippath, vu_open_out op, void *user, char *err, size_t errsz)
{
    FILE *f = fopen(zippath, "rb");
    static unsigned char tail[22 + 65535]; /* static: 64 KB is a lot of stack */
    unsigned char h[46];
    long size, start, i, cdoff, delta = 0;
    unsigned long nent, k;
    size_t got;
    int count = 0;
    if (!f) {
        seterr(err, errsz, "cannot open", zippath);
        return -1;
    }
    /* End of central directory: the last "PK\5\6" within the final 64 KB + 22. */
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 22)
        goto bad;
    start = size > (long)sizeof tail ? size - (long)sizeof tail : 0;
    if (fseek(f, start, SEEK_SET) != 0)
        goto bad;
    got = fread(tail, 1, (size_t)(size - start), f);
    for (i = (long)got - 22; i >= 0; i--)
        if (tail[i] == 'P' && tail[i + 1] == 'K' && tail[i + 2] == 5 && tail[i + 3] == 6)
            break;
    if (i < 0)
        goto bad;
    nent = get16(tail + i + 10);
    cdoff = (long)get32(tail + i + 16);
    /* A self-extracting archive (.ZIPEXE) has the extractor in front: its
     * offsets may be relative to the start of the ZIP part. The central
     * directory ends where the EOCD starts, so the difference is the prefix. */
    {
        long eocd = start + i, cdsize = (long)get32(tail + i + 12);
        delta = eocd - cdsize - cdoff;
        if (delta < 0)
            goto bad;
        cdoff += delta;
    }
    if (get32(tail + i + 16) == 0xFFFFFFFFUL || nent == 0xFFFF) {
        seterr(err, errsz, "ZIP64 archives are not supported", NULL);
        fclose(f);
        return -1;
    }
    for (k = 0; k < nent; k++) {
        char name[256];
        unsigned long nlen, xlen, clen, method, flags, csize, usize, crc, loff;
        const char *b;
        long next;
        FILE *out;
        unsigned char lh[30];
        if (fseek(f, cdoff, SEEK_SET) != 0 || fread(h, 1, 46, f) != 46 || get32(h) != 0x02014b50UL)
            goto bad;
        flags = get16(h + 8);
        method = get16(h + 10);
        crc = get32(h + 16);
        csize = get32(h + 20);
        usize = get32(h + 24);
        nlen = get16(h + 28);
        xlen = get16(h + 30);
        clen = get16(h + 32);
        loff = get32(h + 42);
        if (nlen >= sizeof name || fread(name, 1, nlen, f) != nlen)
            goto bad;
        name[nlen] = '\0';
        next = cdoff + 46 + (long)(nlen + xlen + clen);
        cdoff = next;
        if (nlen && name[nlen - 1] == '/')
            continue; /* a directory entry */
        if (flags & 1) {
            seterr(err, errsz, "encrypted member", name);
            goto fail;
        }
        if (method != 0 && method != 8) {
            seterr(err, errsz, "unsupported compression method", name);
            goto fail;
        }
        if (!(b = base_name(name))) {
            seterr(err, errsz, "unsafe member name", name);
            goto fail;
        }
        /* Local header: its own name and extra lengths come before the data. */
        if (fseek(f, (long)loff + delta, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30
            || get32(lh) != 0x04034b50UL
            || fseek(f, (long)loff + delta + 30 + (long)(get16(lh + 26) + get16(lh + 28)),
                     SEEK_SET) != 0)
            goto bad;
        if (!(out = op(b, user))) {
            seterr(err, errsz, "cannot create", b);
            goto fail;
        }
        if (copy_member(f, out, (int)method, csize, usize, crc) != 0) {
            fclose(out);
            seterr(err, errsz, "damaged member (CRC or length mismatch)", name);
            goto fail;
        }
        if (fclose(out) != 0) {
            seterr(err, errsz, "cannot write", b);
            goto fail;
        }
        count++;
    }
    fclose(f);
    return count;
bad:
    seterr(err, errsz, "not a ZIP archive or damaged", zippath);
fail:
    fclose(f);
    return -1;
}
