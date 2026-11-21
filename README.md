# VUPDATE for OpenVMS

A winget-style updater for OpenVMS: it compares the installed PCSI products with the VSI
Service Platform catalogue and lists newer kits, OpenVMS patches and security fixes, then
downloads, verifies and installs the ones you choose with `PRODUCT INSTALL`. For OpenVMS x86-64
and Alpha.

```
$ VUPDATE LOGIN                         ! once per node: your VSI portal account
$ VUPDATE CHECK                         ! what is new (/SECURITY, /ALL, /OUTPUT=file)
$ VUPDATE SHOW OPENSSH                  ! a kit's files, prerequisites and notes
$ VUPDATE UPGRADE/PATCHES/NOREBOOT      ! every patch that needs no reboot
$ VUPDATE UPGRADE OPENSSH               ! one product
$ @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY START   ! a nightly check (never installs)
$ VUPDATE HELP
```

**Status: early test (0.1.0).** On AXPBOX (Alpha V8.4-2L2) it installed all 38 OpenVMS L2 patches
the portal offers, in three runs, with their hidden prerequisites. See [docs/PLAN.md](docs/PLAN.md)
and [kit/readme.vms](kit/readme.vms) for what UPGRADE does and its limits.

Needs a VSI Service Platform account (sp.vmssoftware.com). VUPDATE is not a VSI product and is
not supported by VSI; the portal's interface is undocumented and may change.

## How it works

- Signs in with your portal account (Keycloak password grant); the password is kept sealed to the
  node (AES-256-GCM, owner-only file). Obfuscation, not secrecy from privileged users.
- Reads `PRODUCT SHOW PRODUCT */FULL`, including applied patches, and the portal catalogue.
- Adds prerequisites from the portal *and* from each kit's release notes (the portal's lists are
  incomplete), skipping those for other OpenVMS versions; orders them; downloads; checks MD5;
  unpacks ZIP and self-extracting kits itself; installs with `/OPTIONS=SIGNED_KIT` and recovery data.
- A failed kit is kept and retried once at the end; reboot kits are installed together and listed
  for one reboot. OpenVMS upgrades are reported, never installed.

## Build

```sh
make                      # Linux: unit tests (core, store, kit) and build/vucheck
tools/image.sh <node>     # node: unit tests, image, dev verb (@dev_setup)
tools/kit.sh <node>       # node: clean build and PCSI kit, fetched to out/kits/
```

libcurl comes from [vms-curl](https://github.com/issinoho/vms-curl)'s static library and is linked
into the image, with zlib and zstd; OpenSSL is VSI SSL3. docs/ has the plan, decisions and notes.

## Licence

MIT (see LICENSE). Includes cJSON 1.7.18 (MIT) in src/third/cjson, and statically libcurl (curl
licence), zlib and zstd (BSD). The kit's CA bundle is the curl project's Mozilla CA bundle (MPL 2.0).
