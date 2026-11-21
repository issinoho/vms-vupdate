# Notes: platform and portal behaviour, and how it was observed

N1. VSI VSP E1.0-3 (x86) is a ~54 MB static image (curl + OpenSSL + json-c). Strings and its
hidden `SET DEBUG` command (curl verbose) showed the API below. Observed 2026-11-21.

N2. **Auth**: Keycloak password grant, public client, no secret:
`POST https://kc.teracloud.com/realms/vmssoftware/protocol/openid-connect/token`,
body `client_id=sp&grant_type=password&username=<email>&password=<pw>`. The access token is a JWT
valid for 180 s, sent as `Authorization: Bearer`. Not yet called by us (needs the password).

N3. **API** (https://sp.vmssoftware.com, nginx, `x-app-version: 3.1-1`), all JSON, verified with curl:
- `GET /vsp/whoami`: user, org, permission flags (package_read ...).
- `PUT /vsp/pkgman/packages` with `{"sysarch":["X86"|"I64"|"AXP"], "name"?, "version"?,
  "minos"?:[...], "latestver"?:0|1}`. sysarch is required. On a bad body the server returns its
  JSON schema in `reason`. latestver made no difference. Rows: name, version, package_number,
  type, sysarch, minos (a *string*: the first value only), release_date, verified, description.
- `GET /vsp/pkgman/packages/package` with header `X-Pkg-Id: <package_number>`: one-element
  array: files[] (filename, save_as, download_size, md5, package_file_number), prereq[]
  (name, package_number, installation_order, minos, has_access), required_for[], minos (an
  *array*), restart_required "0"/"1", eco_rating, contracts.
- `GET /vsp/pkgman/packages/package/download` with `X-Pkg-Id: <package_file_number>` and
  `X-Os-Version`: `{"success":true,"urls":[{"order":1,"url":"<uuid>"}]}`, then
  `GET /vsp/pkgman/packages/package/download/<uuid>` returns the ZIP.
- Errors: `{"success":false,"reason":"..."}`; 401 without a token.

N4. Types: E patch/ECO, L layered product, O open source, V OpenVMS, D licence bundle, W Windows.
Counts on 2026-11-21: X86 181, I64 247, AXP 161 rows. The Alpha catalogue is served although VSP
doesn't run on Alpha.

N5. **VSP and curl hang over non-interactive ssh** (`ssh host cmd`) on the x86 node; they work in a
pty session or with /OUTPUT (tools/vms.sh). Same for PRODUCT SHOW PRODUCT without /OUTPUT.

N6. **PRODUCT SHOW PRODUCT * /FULL** columns: PRODUCT, KIT TYPE, STATE, MAINTENANCE, REFERENCED BY,
with a dash line under the header giving column widths. Applied patches are listed in
MAINTENANCE of the product they patch (Alpha: `VSI AXPVMS VMS842L2A_RTL V10.0` under VMS),
one per line, continuing on lines with a blank PRODUCT column. `/KIT_TYPE` is not a qualifier
of SHOW PRODUCT on V8.4-2L2.

N7. The VSI CURL kit's LIBCURL.OLB on x86 (8.13.0) can't be linked standalone: nghttp2 (165),
libssh2 (119), LDAP (37+2) and zlib symbols undefined. Alpha's is 8.0.1. See D3.

N8. VSI C resolves `#include <curl/curl.h>` as `CURL:CURL.H`: define the logical CURL as the
directory holding curl.h; /INCLUDE doesn't do it.

N9. Linking vms-curl's CURLLIB.OLB gives `%LINK-W-WRNERS` on Alpha and `%ILINK-W-COMPWARN` on x86
(compile warnings recorded in curl's modules: vtls, sha256, openssl ...), as in vms-curl's own build. build_vms.com accepts that
warning only, and fails on UDFSYM/USEUNDEF/-E-/-F-.

N10. Probes 2026-11-21, x86 E9.2-4 and Alpha V8.4-2L2 alike: libcurl 8.22.0 + OpenSSL 3.0.22; sp.vmssoftware.com 401 (no token),
kc.teracloud.com realm document 200, using curl.se's cacert-2026-09-25.pem.

N11. The Alpha node (V8.4-2L2) has NOTARY V2.0, DPML V2.0 and RTL V10.0 installed and is missing 38 L2
patches (SYS V5.0, DCL V4.0, RMS V6.2 ...): the L1 patches were removed by the L1 -> L2 upgrade
on 2026-11-08 (PRODUCT SHOW HISTORY). The x86 node (E9.2-4) has no applicable patches on the portal yet.

N12. A concealed rooted logical (ZLIB$ROOT) can't be defined on top of another rooted logical
(USER$ROOT:): ILINK-F-OPENIN / RMS-F-DEV. Resolve with F$PARSE(...,"NO_CONCEAL") first, as
vms-curl's build.sh does (tools/env.com.example).

N13. MESSAGE sets the facility-specific bit (%X8000) as well as the customer bit (bit 27): VUPDATE
CURRENT is %X0F6F8009, not %X0F6F0009. MESSAGE/LIST shows each value. Observed x86, 2026-11-21.

N14. In DCL an unquoted `@` in a qualifier value starts an indirect command file:
`VUPDATE LOGIN/USERNAME=a@b.c` fails with DCL-E-OPENIN for `b.c`. Quote it: `/USERNAME="a@b.c"`.

N15. Keycloak answers a password grant for an unknown user with HTTP 401 and
`{"error":"invalid_grant","error_description":"Invalid user credentials"}` (made-up account
vupdate-test@example.invalid; x86 and Alpha, 2026-11-21). libcurl takes the CA bundle as a VMS file
spec (VUPDATE$ROOT:[DATA]CACERT.PEM) on both.

N16. Spawned `PRODUCT SHOW PRODUCT * /FULL` (LIB$SPAWN, output to a SYS$SCRATCH file) works in
CHECK, and a 120-block single-line JSON cache file (Stream_LF) reads back with fread on x86.
