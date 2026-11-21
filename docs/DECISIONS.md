# Decisions

D1 (user, 2026-11-21). **C + libcurl**, shipped as an ISSINOHO PCSI kit like the rest of the
vms-* family. Rejected: Python (VSI Python is x86-only), DCL around curl (JSON and version
comparison too fragile).

D2 (user, 2026-11-21). **Own credential store** with its own LOGIN, independent of VSI VSP.
Rejected: reusing VSP's `VSP$ROOT:<user>.CFG` (undocumented scrambled format, and VSP doesn't
exist on Alpha); prompting every run (rules out the nightly check). The store is obfuscation,
not encryption, and the docs must say so.

D2a (user, 2026-11-21). **Targets: x86-64 and Alpha. Not IA64.** v1 extras: nightly batch check
(never installs), opt-in field-test kits, a not-in-catalogue report.

D3 (2026-11-21, phase 0). **libcurl: vms-curl's static CURLLIB.OLB** (curl 8.22, OpenSSL via
VSI SSL3 shareables, zlib and zstd from vms-zlib/vms-zstd). The first choice was the VSI CURL
kit's `SYS$COMMON:[CURL.LIB]LIBCURL.OLB`, but on x86 (8.13.0) that library needs nghttp2,
libssh2 and LDAP, which the kit doesn't ship (284 undefined symbols), and on Alpha it is 8.0.1
from 2023. vms-curl builds the same way on both targets and is ours to update. Build locations come
from logicals (tools/env-<node>.com, git-ignored). Open: vms-curl's kit ships no library yet.

D4 (2026-11-21). **Versions come from the kit name, not the portal's version field**, when the
name holds one (`-[VAXTEB]MMmm` or `-[VAXTEB]N.N`). The field is inconsistent:
`A10.1-3250403` for kit `...-A1001-3_250403-1` (PCSI says A10.1-3_250403), `V1.10-14A-1` for
`...-V0110-14A-1` (PCSI V1.10-14A). The final all-digit group after another group is the kit
revision and is dropped. Names with letters after MMmm (`V0909A-2`) fall back to the field.

D5 (2026-11-21). **Version comparison**: stage letter ignored; numbers numeric; letter runs as
strings; separators ignored; trailing zeros dropped (V9.2 == V9.2-0); letters older than a
number in the same place. Consequence: LUA V5.3-5H is current (the prototype's "V5.3-5H-1
update" was the kit revision).

D6 (2026-11-21). **Release vs field test**: only V (or no letter) is a release. A/B/E/T/X kits are
offered only with /FIELD_TEST, or when the installed version is itself non-release.

D7 (2026-11-21). **Applicability**: patches (type E) need the running version in minos, or a
family code that names it (VMS923X_ = V9.2-3, VMS842L2A_ = V8.4-2L2). Catalogue rows carry only the
*first* minos (DCL V4.0: row V9.2-2, detail [V9.2-2, V9.2-3]), so vu_patch_needs_detail picks
kits whose detail must be fetched and merged. Products: running version >= the lowest minos.
OS kits: always (they are reported, never installed).

D8 (2026-11-21). **Installed patches come from PCSI's MAINTENANCE column** of PRODUCT SHOW
PRODUCT /FULL. Only the newest applicable kit of each patch family is offered (V5.0 supersedes
V4.0). ECOs for layered products (e.g. PERFMGMT ECO1) only when the product is installed.

D9 (2026-11-21). **Producers matched**: VSI, HP, HPE, DEC, CPQ, COMPAQ. Other producers'
products (ISSINOHO, VMSPORTS, SCI, MIMER...) are never matched by name.

D10 (2026-11-21). **"Critical" = security text**: eco_rating was "1" on every patch seen, so it
can't rank. A patch counts as a security fix when its description contains CVE-, security,
vulnerab or exploit. restart_required marks reboot kits.

D11 (2026-11-21). **Committed fixtures are synthetic or our own output**: PRODUCT SHOW PRODUCT
listings from the nodes, and catalogue subsets with VSI's descriptions replaced. Real portal
replies stay in git-ignored tests/data/real/ and add checks when present.

D12 (2026-11-21). **JSON: cJSON 1.7.18**, vendored unmodified in src/third/cjson (MIT), compiled
without our warning rules.

D13 (2026-11-21, phase 1). **Credential store: AES-256-GCM** (OpenSSL libcrypto, already linked
for curl) under SHA-256 of a label, the node key (SCSNODE | SCSSYSTEMID | HW_NAME) and a random
salt; text file (magic line + hex). Rejected: plain XOR scrambling as VSP does (no tamper check,
and it reads as security it isn't). Still obfuscation (D2): the key comes from values any process
can read, so file protection (owner only) is the real barrier; the sealing keeps plaintext out of
backups, makes a copied file useless on another node, and detects edits. Changing SCSNODE means
VUPDATE LOGIN again.

D14 (2026-11-21, phase 1). **Messages and exit status**: a MESSAGE file (facility VUPDATE, 1903)
linked into the image. CHECK exits with VU__CURRENT (success) when there is nothing to do and
VU__UPDATES (informational) when there is, so batch jobs can test $STATUS. The C code computes the
condition values itself (VU_MSG in vupdate.c), checked against MESSAGE/LIST (N13).

D15 (2026-11-21, phase 1). **CHECK signs in only when it has to fetch**: the catalogue is cached for
1 h and package details for 24 h in VUPDATE$ROOT:[CACHE] (/REFRESH skips the cache). A CHECK answered
from the cache needs neither credentials nor the network. Patch details are fetched for the
D7 minos merge and for the flags (security text, restart_required) of the patches reported.

D16 (2026-11-21, phase 2). **VUPDATE extracts kit ZIPs itself** (src/app/vu_kit.c, zlib inflate, CRC
checked, ZIP64 and encryption refused, member names flattened and checked). The Alpha has no unzip
and x86 only an unsupported one. PCSI kit files are written as fixed 512-byte records (VSI's
instruction for transferred kits); manifests and other files as Stream_LF. Self-extracting .ZIPEXE
kits (offsets not adjusted) are handled by working out the prefix from the end-of-directory record.

D17 (2026-11-21, phase 2). **Prerequisites**: from each kit's detail, transitively; those already
installed (product at that version or newer) or made for another OpenVMS version (L1 variants listed
on L2 kits, N19) are skipped. Order: vu_install_order (prerequisites first, else plan order).
installation_order is per kit and ignored across kits. OS upgrades are never installed.

D18 (2026-11-21, phase 2). **Install command**: `PRODUCT INSTALL <product> /PRODUCER /BASE_SYSTEM
/SOURCE=<kit dir> [/SAVE_RECOVERY_DATA for patches] /OPTIONS=(SIGNED_KIT,NOCONFIRM)` in a subprocess
on the user's terminal. SIGNED_KIT makes PCSI refuse a kit that fails VSI signature validation. UPGRADE
asks before installing (needs a terminal) unless /NOCONFIRM; stops after a kit that needs a reboot.
The kit directory is deleted after a successful install, kept on failure and with /DOWNLOAD_ONLY.

D19 (2026-11-21, phase 3). **UPGRADE selectors and /NOREBOOT**: /PATCHES and /UPDATES select all
findings of that kind (beside names, /ALL, /SECURITY). /NOREBOOT drops kits with restart_required and,
transitively, kits needing a dropped kit. VUPDATE asks once for the whole list, so PCSI's own
selection question is always skipped (NOCONFIRM); kit-specific questions still come. The OS product
VMS ("Oper System") is no longer reported: OPENVMS stands for the OS.

D20 (2026-11-21, phase 3). **Help**: a DCL help library built from src/plat/vms/vupdate.hlp into
VUPDATE$ROOT:[HELP]VUPDATE.HLB, shown by `VUPDATE HELP [topic...]` (LBR$OUTPUT_HELP, prompting on a
terminal). Inserting it into SYS$HELP:HELPLIB.HLB, for plain `HELP VUPDATE`, is a kit-install
question (system-wide: ask the user, phase 4).
