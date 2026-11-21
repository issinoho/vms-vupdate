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
