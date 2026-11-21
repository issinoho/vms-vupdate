# VUPDATE for OpenVMS

A winget-style updater for OpenVMS: it compares the installed PCSI products with the VSI
Service Platform catalogue and offers newer kits, OpenVMS patches and security fixes, then
downloads, verifies and installs them with `PRODUCT INSTALL`. For OpenVMS x86-64 and Alpha.

**Status: in development (phase 0 of 4 done).** The matching core and its tests run on Linux,
x86-64 and Alpha; the DCL command doesn't exist yet. See [docs/PLAN.md](docs/PLAN.md).

Needs a VSI Service Platform account (sp.vmssoftware.com). VUPDATE is not a VSI product and is
not supported by VSI.

## Build (development)

```sh
make                      # Linux: unit tests and build/vucheck
tools/push.sh <node>      # copy to the node's work directory
tools/probe.sh <node>     # libcurl + TLS probe against the portal (PROBE-OK)
```

On the node: `@build_vms TEST`.

## Licence

MIT (see LICENSE). Includes cJSON 1.7.18 (MIT), in src/third/cjson.
