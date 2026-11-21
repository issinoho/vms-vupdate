/* test_kit.c - MD5 and ZIP extraction (src/app/vu_kit.c) on the synthetic
 * tests/data/kit_test.zip (made with Python zipfile: a deflated .PCSI$COMPRESSED,
 * a stored README in a subdirectory, a directory entry) and kit_evil.zip (a member
 * named with a VMS device and directory), kit_sfx.zipexe (kit_test.zip behind 1000
 * bytes of fake extractor, offsets not adjusted, as in a self-extracting kit). Linux: make test; nodes: @build_vms TEST. */
#include "vu_kit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
static char got[4][64];
static int ngot;

#define CHECK(c)                                                                         \
    do {                                                                                 \
        checks++;                                                                        \
        if (!(c)) {                                                                      \
            fails++;                                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                          \
        }                                                                                \
    } while (0)

#ifdef __VMS
#define OUTDIR "[.build]"
#else
#define OUTDIR "build/"
#endif

static FILE *open_out(const char *name, void *user)
{
    char path[256];
    (void)user;
    if (ngot < 4)
        strcpy(got[ngot++], name);
    snprintf(path, sizeof path, OUTDIR "%s", name);
    return fopen(path, "wb");
}

int main(void)
{
    char hex[33], err[256];
    int n;
    CHECK(vu_md5_file("tests/data/no-such-file.zip", hex) == -1);
    /* Also shows the copy on a node is byte-exact (sftp put). */
    CHECK(vu_md5_file("tests/data/kit_test.zip", hex) == 0
          && strcmp(hex, "f933fc2283ca790c9c70890928f2204d") == 0);
    n = vu_unzip("tests/data/kit_test.zip", open_out, NULL, err, sizeof err);
    if (n < 0)
        printf("unzip: %s\n", err);
    CHECK(n == 2);
    CHECK(ngot == 2 && strcmp(got[0], "VSI-X86VMS-TESTKIT-V0100--1.PCSI$COMPRESSED") == 0
          && strcmp(got[1], "README.TXT") == 0);
    CHECK(vu_md5_file(OUTDIR "VSI-X86VMS-TESTKIT-V0100--1.PCSI$COMPRESSED", hex) == 0
          && strcmp(hex, "9f3b6b9447eccd41e6843718e561ab2e") == 0);
    remove(OUTDIR "VSI-X86VMS-TESTKIT-V0100--1.PCSI$COMPRESSED");
    remove(OUTDIR "README.TXT");
    /* Self-extracting: 1000 bytes of "extractor" in front, offsets not adjusted. */
    ngot = 0;
    n = vu_unzip("tests/data/kit_sfx.zipexe", open_out, NULL, err, sizeof err);
    if (n < 0)
        printf("unzip sfx: %s\n", err);
    CHECK(n == 2);
    CHECK(vu_md5_file(OUTDIR "VSI-X86VMS-TESTKIT-V0100--1.PCSI$COMPRESSED", hex) == 0
          && strcmp(hex, "9f3b6b9447eccd41e6843718e561ab2e") == 0);
    remove(OUTDIR "VSI-X86VMS-TESTKIT-V0100--1.PCSI$COMPRESSED");
    remove(OUTDIR "README.TXT");
    ngot = 0;
    CHECK(vu_unzip("tests/data/kit_evil.zip", open_out, NULL, err, sizeof err) == -1);
    CHECK(ngot == 0 && strstr(err, "unsafe") != NULL);
    CHECK(vu_unzip("tests/data/pss_x86.txt", open_out, NULL, err, sizeof err) == -1);
    printf("kit: %d checks, %d failed\n", checks, fails);
    return fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
