#!/usr/bin/env bash
# kit.sh <node> - build VUPDATE on <node> from clean (TEST, then IMAGE), make the PCSI
# kit there and fetch it to out/kits/, with SHA256SUMS. (After vms-vtree's kit.sh.)
#
# The version comes from src/core/vu_version.h alone; MAKE_KIT.COM refuses to
# package an image that reports a different one.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: kit.sh <node>}
: "${KIT_PRODUCER:=ISSINOHO}"
KIT_PRODUCT=VUPDATE
version=$(sed -n 's/^#define VU_VERSION *"\(.*\)"/\1/p' "$top/src/core/vu_version.h")
pcsiversion=$(sed -n 's/^#define VU_PCSI_VERSION *"\([^"]*\)".*/\1/p' "$top/src/core/vu_version.h")
[ -n "$version" ] && [ -n "$pcsiversion" ] || { echo "kit: no version in vu_version.h" >&2; exit 1; }
cacert=${VU_CACERT:-$HOME/projects/vms-curl/cache/cacert-2026-09-25.pem}
[ -f "$cacert" ] || { echo "kit: no CA bundle $cacert (set VU_CACERT)" >&2; exit 1; }
read -r _ ARCH _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
case ${ARCH^^} in X86*) base=X86VMS; archname=x86-64 ;; ALPHA*|AXP*) base=AXPVMS; archname=Alpha ;;
    *) echo "kit: unsupported architecture '$ARCH' (x86-64 and Alpha only)" >&2; exit 1 ;; esac

# Generated kit inputs: never committed (they carry the version), uploaded to [.KIT].
stage=$top/out/kitstage-$node  # per node: kits for both may be built at once
rm -rf "$stage"; mkdir -p "$stage" "$top/out/kits"
subst() { sed -e "s/@PRODUCER@/$KIT_PRODUCER/g" -e "s/@BASE@/$base/g" \
              -e "s/@PCSIVERSION@/$pcsiversion/g" -e "s/@VERSION@/$version/g" -e "s/@ARCH@/$archname/g"; }
subst < "$top/kit/vupdate.pcsi\$desc_template" > "$stage/PRODUCT-$base.PCSI\$DESC"
subst < "$top/kit/vupdate.pcsi\$text_template" > "$stage/PRODUCT-$base.PCSI\$TEXT"
subst < "$top/kit/readme.vms" > "$stage/README.VMS"
cp "$top/kit/make_kit.com" "$top/kit/vupdate\$startup.com" "$top/kit/vupdate\$setup.com" \
   "$top/kit/vupdate_nightly.com" "$stage/"
cp "$cacert" "$stage/CACERT.PEM"
printf 'KIT_PRODUCER=%s\nKIT_PRODUCT=%s\nPCSI_VERSION=%s\nKIT_VERSION=%s\n' \
    "$KIT_PRODUCER" "$KIT_PRODUCT" "$pcsiversion" "$version" > "$stage/KIT.ENV"

echo "kit: VUPDATE $version ($KIT_PRODUCER $base $KIT_PRODUCT $pcsiversion) on $node"
"$top/tools/push.sh" "$node" > /dev/null
"$top/tools/vms.sh" "$node" put "$stage"/* -- kit > /dev/null
"$top/tools/vms.sh" "$node" put "tools/env-$node.com" -- build_ca > /dev/null
job=$top/out/kit-$node.com
cat > "$job" <<DCL
\$ set noon
\$ set default $WORKDIR
\$ @[.build_ca]env-$node.com
\$ @build_vms TEST
\$ if .not. \$status then exit \$status
\$ @build_vms IMAGE
\$ if .not. \$status then exit \$status
\$ @[.kit]make_kit
DCL
mode=run; [ "${ARCH^^}" = ALPHA ] && mode=batch
"$top/tools/vms.sh" "$node" $mode "$job" | tee "$top/out/kit-$node.log" |
    grep -E 'checks|TEST-OK|IMAGE-OK|MAKE_KIT|BUILD-FAILED|%PCSI|-E-|-F-' || true
kit=$(sed -n 's/^MAKE_KIT: kit .*\]\([^;]*\);.*/\1/p' "$top/out/kit-$node.log" | head -1)
[ -n "$kit" ] || { echo "kit: no kit produced (see out/kit-$node.log)" >&2; exit 1; }
"$top/tools/vms.sh" "$node" get "KIT_$ARCH/$kit" "$top/out/kits/$kit"
(cd "$top/out/kits" && sha256sum "$kit" | tee "SHA256SUMS-$base")
