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

N17. Phase 1 on the real account, 2026-11-21 (user typed LOGIN on both nodes). X86VMS E9.2-4: 9 updates
(APACHEWS COBOL CXX LIBPQ PYTHON PYTHWHLS SQLRELAY VMSSPI X86ASM), no patches; 20 details fetched; output
identical to build/vucheck on the same PRODUCT listing and catalogue. AXPBOX V8.4-2L2: 38 VMS842L2A
patches (14 flagged reboot, DCL V4.0 flagged security) + CURL, KERBEROS, OPENSSH updates. Repeat CHECK
answered from the cache without signing in. /SECURITY lists DCL V4.0 only. The x86 store copied to the
Alpha is refused (VU__STORE). Store file protection (S:RWD,O:RWD,G,W). PRODUCT SHOW PRODUCT has no /OUTPUT.

N18. **Download is three steps** (corrects N3, which assumed two). (1) `GET .../package/download` with
X-Pkg-Id: <package_file_number> and X-Os-Version returns `urls[]` of ids for the requested file *and
the files of its prerequisites* (FILESYS .TXT: 4 ids, three of them NOTARY's .ZIPEXE, .ZIPEXE_VNC and
.TXT; "order" is the install order). (2) `GET .../download/<id>` returns
`{"md5","save_as","url"}`, url being a pre-signed DigitalOcean Spaces (S3) link with a short expiry.
(3) GET that link, without the bearer token. VUPDATE asks step 2 for each id and fetches only the one
whose save_as is the file wanted. Alpha, 2026-11-21.

N19. Patch details list prerequisites for every OpenVMS version a kit family covers: L2 kits name
VMS842L1A_NOTARY *and* VMS842L2A_NOTARY (RMS V6.2 also both FILESYS variants). Kits come as 1-3 files
(.ZIP, plus .TXT release notes; NOTARY is a self-extracting .ZIPEXE). A ZIP holds one
.PCSI$COMPRESSED and its signature manifest .PCSI$COMPRESSED_VNC. Extracted as fixed-512 + Stream_LF,
`PRODUCT LIST` validates them (%PCSI-I-VSIVALPASSED): TIMEZONE, FILESYS, F11X, PTHREAD, SORT on AXPBOX.

N20. PRODUCT INSTALL has no /CONFIRM qualifier; NOCONFIRM is an /OPTIONS keyword, as are
SIGNED_KIT and NOVALIDATE_KIT (V8.4-2L2 HELP).

N21. **First real install** (2026-11-21, user at the AXPBOX terminal): `VUPDATE UPGRADE
VMS842L2A_TIMEZONE` installed the patch; PRODUCT SHOW HISTORY: `VSI AXPVMS VMS842L2A_TIMEZONE V1.0
Patch Install Val`, kit directory removed afterwards. PCSI records V1.0, while the portal kit name
`VMS842L2A_TIMEZONE-V0100-4` (and its version field V1.0-4) read as V1.0-4, so CHECK still offered it.
The `-4` is the PCSI kit type (the kit file is `...-V0100--4`). The decoder now drops a lone digit
after the version of an OpenVMS patch-family name; only the TIMEZONE kits have one (x86, Alpha).

N22. **The portal's prerequisite lists are incomplete; the release notes are not.** Phase 3 run
(2026-11-21, user at AXPBOX): 13 patches installed, then VMS842L2A_LCKMGRSTB V2.0 failed in VSI's kit
procedure: `%VSIKIT-F-NOPREREQ ... only valid if the VMS842L2A_LCKMGR-V0100 kit has been applied`
(PCSI recovery pass ran, nothing left half-installed). The portal lists only NOTARY for it. Every
patch's .TXT release notes have a "4  KIT DEPENDENCIES:" section, which also names e.g. SYS V3.0
for FILESYS. Its entries can be alternatives ("L1A_x or L2A_x"), recommended rather than required
(PWDMGMT: PCSI V1.0), spelt with "_V0200" for "-V0200", or shipped inside the kit's own container:
VMS842L2A_PWDMGMT-V0200.ZIPEXE (a real self-extracting archive) holds PWDMGMT plus the L1A and L2A
PCSI V1.0 kits, and VMS842L2A_PCSI is not in the portal catalogue.

N23. Second /PATCHES/NOREBOOT run (2026-11-21, user at AXPBOX): 9 of 11 kits installed (LDAP, LMF, MIME,
PRTSMB, PTHREAD, PCSI V1.0 then PWDMGMT from one archive, SHUTDOWN, SORT, TIMEZONE). MISC failed in VSI's
kit procedure (needs FILESYS V1.0 and UTILITY V2.0; MISC's notes are a PDF, so unseen) and RMSDEF
declined (`%VSIKIT-I-NOTNEEDED`: only with FILESYS), both rolled back. TIMEZONE's own dialogue says
"This ECO kit requires a system reboot" while the portal's restart_required is 0: the portal flag
is not authoritative. Post-install steps named by kits: SET SERVER ACME_SERVER/RESTART (LDAP, PWDMGMT),
UCM RESTART (MISC), restart images using PTHREAD$RTL, @SYS$MANAGER:UTC$TIME_SETUP in the affected
Canadian zones (TIMEZONE). PCSI-I-RETAIN: PWDMGMT kept the newer SYS$STARLET_C.TLB already there.

N24. Reboot-kit run (2026-11-21, user at AXPBOX, UPGRADE/PATCHES): all 16 installed in one run. MISC and
RMSDEF failed in pass 1 (FILESYS/UTILITY not yet in), and installed in the retry pass (D22). Afterwards
CHECK lists no patches: all 38 L2 patches plus VMS842L2A_PCSI V1.0 went in on 2026-11-21 (39 items in
PRODUCT SHOW HISTORY). Several kits retained newer files already on the system (PCSI-I-RETAIN). The node
needs the reboot the kits ask for.
