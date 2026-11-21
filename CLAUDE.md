# CLAUDE.md

Working notes for vms-vupdate (VUPDATE): a winget-style updater for OpenVMS that compares
the PCSI inventory with the VSI Service Platform catalogue and offers newer kits, OS patches
and security fixes. Targets **x86-64 and Alpha; not IA64** (D2a). `docs/PLAN.md` is the phased
plan, `docs/DECISIONS.md` the decisions, `docs/NOTES.md` portal and platform behaviour and how
it was observed. Read them before starting work. Work phase by phase; stop and ask at each
phase exit.

Green-field code. The portal API is VSI's and undocumented (docs/NOTES.md N2-N3): treat any
change in reply shape as an error to report clearly, never guess.

## Ground rules (from the sibling vms-* repos)

- **Use `tools/vms.sh`** for remote work (`push.sh`, `probe.sh` wrap it). Never raw
  `ssh host cmd` (VSP, curl and PRODUCT hang without a pty, N5), never `WAIT` over ssh.
  The Alpha node runs jobs in batch (`vms.sh alpha batch`). Pass DCL specs in single quotes.
- **Committed files must not contain real node details** (they live in git-ignored
  `tools/nodes.conf` and `tools/env-<node>.com`), **credentials, tokens or portal replies**
  (real replies go in git-ignored `tests/data/real/`, D11). Never print the stored credential,
  a VSP `.CFG` file or a bearer token.
- **Nothing system-wide on the nodes without asking**: no PRODUCT INSTALL/REMOVE, no
  SYSTARTUP/SYLOGIN edits, no system logicals, no DCLTABLES changes (test CLDs with
  `SET COMMAND/TABLES=SYS$LIBRARY:DCLTABLES/OUTPUT=[.build]x.exe`). Downloads of portal kits
  go to the work directory and are deleted after the test.
- Prefer a small probe in `probes/` over speculation. Don't claim something works until a
  test on the node shows it.

## Hard constraints

- **C99, VSI C (x86 7.7, Alpha 7.4), 32-bit pointers**, no VLAs, no C11. Linux build:
  `-std=c99 -pedantic -Wall -Wextra -Werror`. Alpha adds `/FLOAT=IEEE_FLOAT`.
- **`src/core/` does no I/O**: it takes strings (PRODUCT SHOW output, JSON replies) and returns
  structures. Network, files, PCSI and credentials live outside it.
- cJSON (src/third/cjson) is vendored unmodified (D12).
- libcurl comes from vms-curl's CURLLIB.OLB (D3) via logicals set by `tools/env-<node>.com`.

## Testing

`make` (Linux: unit tests + `build/vucheck`). `build/vucheck <pss.txt> <cat.json> <os-ver>
<X86|AXP> [field]` prints a plan from saved files. On the nodes: `tools/push.sh <node>` then
`@build_vms TEST` (x86: `vms.sh x86 dcl '@build_vms TEST'`; Alpha: a batch job), and
`tools/probe.sh <node>` for the libcurl/TLS probe (PROBE-OK).

## OpenVMS

Undefined symbols are *warnings* on VMS: build_vms.com checks link logs (N9). VSI C opens
`<curl/curl.h>` as `CURL:CURL.H` (N8). The C RTL lowercases unquoted argv.

## Commits

Commit in logical steps with messages that explain why. Record what was verified and how.
Don't push unless the user asks.
