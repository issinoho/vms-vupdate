# VUPDATE plan

A winget-style updater for VSI kits: compare the PCSI inventory with the VSI Service Platform
catalogue, then offer to download and install newer kits, OS patches and security fixes.
Started 2026-11-21.

Working name VUPDATE, repo `issinoho/vms-vupdate`, kit `ISSINOHO <arch>VMS VUPDATE`.
Decisions (user, 2026-11-21): C + libcurl; own credential store; v1 extras: nightly batch
check, opt-in field-test kits, a not-in-catalogue report. v1 targets **x86-64 and Alpha**;
IA64 is out of scope (user, 2026-11-21). Code must build with VSI C 7.4 on Alpha V8.4-2L2 as well as
VSI C 7.7 on x86: ANSI C, 32-bit pointers, no x86-only RTL.

Background: docs/NOTES.md (portal API, auth, catalogue fields, PCSI output). Decisions: docs/DECISIONS.md.

## Commands (DCL CLD verb)
| Command | Does |
|---|---|
| `VUPDATE LOGIN` | prompts for email and password, tests them against Keycloak, saves to the store |
| `VUPDATE LOGOUT` | deletes the stored credential |
| `VUPDATE CHECK [/SECURITY] [/FIELD_TEST] [/ALL] [/OUTPUT=f]` | inventory vs catalogue: updates, OS patches, OS upgrades; `/ALL` adds the not-in-catalogue list |
| `VUPDATE UPGRADE <product> \| /ALL \| /SECURITY [/NOCONFIRM] [/FIELD_TEST] [/DOWNLOAD_ONLY] [/WORK=dir]` | resolve prerequisites, download, verify MD5, unzip, `PRODUCT INSTALL` in order |
| `VUPDATE SHOW <product>` | portal detail: description, files, prereqs, restart flag, minos |

Exit status: success = nothing to do; informational code = updates available (for batch use).

## Modules
1. **http.c**: libcurl easy handle. CA bundle shipped with the kit. Proxy via logical
   VUPDATE$PROXY. Token cache in memory, refreshed every 180 s (use refresh_token if Keycloak returns one).
2. **auth.c / store.c**: Keycloak password grant (client_id=sp). Store at
   `VUPDATE$ROOT:[DATA]CREDENTIALS.DAT`, owner SYSTEM, protection (S:RWED,O:RWED,G,W).
   Scrambled with a per-node key derived from SCSSYSTEMID + hardware ID, so a copied file doesn't
   work elsewhere. Document honestly that this is obfuscation, not encryption: anyone with
   SYSPRV/BYPASS can recover it.
3. **inventory.c**: PCSI product list. Preferred route: the PCSI callable interface, if
   documented on VSI kits. Fallback: spawn `PRODUCT SHOW PRODUCT */FULL` to a file and parse it.
   Also `PRODUCT SHOW HISTORY` for patches already installed. OS version from SYI$_VERSION, arch
   from SYI$_ARCH_NAME.
4. **catalog.c**: PUT /vsp/pkgman/packages {"sysarch":[arch]}, then filter on minos ∋ current OS.
   Detail via GET .../package (X-Pkg-Id), fetched only for candidates. Cache the catalogue for
   1 h in VUPDATE$ROOT:[CACHE].
5. **match (src/core, done in phase 0)**: vu_ver (version compare, kit-name decoding), vu_pcsi
   (PRODUCT SHOW PRODUCT parser incl. the MAINTENANCE column), vu_cat (catalogue/detail JSON),
   vu_plan (updates, patches, OS upgrades, not-in-catalogue, install order). See D4-D9.
6. **patch.c**: type E kits for this OS version not yet installed. Topological order from
   prereq.installation_order. Flags: SECURITY when the description matches CVE-/security/
   vulnerability/exploit; REBOOT when restart_required=1. Type V newer than running = "OS upgrade
   available" (report only, never installed).
7. **install.c**: GET download → uuid → GET download/<uuid> to VUPDATE$ROOT:[KITS], then MD5
   check (skipped with a warning when the portal has none), unzip, find exactly one .PCSI or
   .PCSI$COMPRESSED, and `PRODUCT INSTALL <name>/SOURCE=dir/KIT_TYPE=.../SAVE_RECOVERY_DATA`
   (patches) interactively, or /NOCONFIRM. Stop the run after a REBOOT kit.
8. **JSON**: bundle cJSON (MIT; one .c file, ANSI C).

## Batch check
`VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY.COM` resubmits itself daily on SYS$BATCH and runs `VUPDATE CHECK`.
On change it reports via OPCOM; VMSNOTIFY and MAIL are optional, set by logical. It never installs.
Like SSHLOGS in vms-dcl-tools.

## Phases
0. **Done 2026-11-21.** Repo scaffold, CLD draft, build_vms.com for x86 and Alpha, core library
   + unit tests (Linux, x86, Alpha), libcurl link probe on both (vms-curl CURLLIB.OLB + SSL3:
   libcurl 8.22.0, OpenSSL 3.0.22, both portal hosts reached).
1. LOGIN/LOGOUT/CHECK on the x86 node, then the Alpha. Check the output against tests/unit/vucheck on
   saved replies. **Done 2026-11-21** (N17).
2. SHOW + UPGRADE with /DOWNLOAD_ONLY, then a real install of a low-risk kit (ask first; LUA
   turned out to be current, see D5) with SAVE_RECOVERY_DATA. **Done 2026-11-21** (N18-N21):
   downloads verified on AXPBOX; VMS842L2A_TIMEZONE installed by the user with VUPDATE UPGRADE.
3. **Done 2026-11-21** (N22-N24): all 38 L2 patches on AXPBOX installed through VUPDATE in three runs.
   Patch path: prereq ordering + security flag, tested on the Alpha (V8.4-2L2 has SYS-V0500 etc.). The x86 node
   (E9.2-4) has no patches yet.
4. Nightly batch, x86 + Alpha kits, release per the vms-* family process.
Not planned: IA64.

## Open questions / risks
- The portal API is undocumented and VSP-internal: x-app-version 3.1-1 today, and it may change.
  Detect schema errors and say so plainly.
- Keycloak may require MFA for some accounts later. The password grant then fails, so give a clear error.
- Whether the portal's terms allow third-party clients: worth asking VSI (ambassador channel).
- Kits with several PCSI files or VMSINSTAL kits: report and skip (VSP has the same limit).
- Products whose portal name differs from the PCSI name: keep a small alias table in the kit.
- libcurl: vms-curl ships no library in its kit, so the build uses its build tree (D3). For
  releases, add CURLLIB.OLB + headers to the VMSCURL kit, or a VMSCURL_DEV kit (needs a vms-curl release).
