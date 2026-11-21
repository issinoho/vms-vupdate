#!/usr/bin/env bash
# probe.sh <node> - push, then build and run probes/curl_probe on the node with the
# library locations from tools/env-<node>.com. Prints PROBE-OK on success.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: probe.sh <node>}
v=$top/tools/vms.sh
cd "$top"
tools/push.sh "$node" >/dev/null
$v "$node" put "tools/env-$node.com" ~/projects/vms-curl/cache/cacert-2026-09-25.pem -- build_ca
job=$(mktemp --suffix=.com)
read -r _ ARCH _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
cat > "$job" <<DCL
\$ set noon
\$ set default $WORKDIR
\$ @[.build_ca]env-$node.com
\$ @build_vms PROBE build_ca/cacert-2026-09-25.pem
DCL
mode=run; [ "$ARCH" = ALPHA ] && mode=batch
cp "$job" "$top/out/probe-$node.com" 2>/dev/null || { mkdir -p "$top/out"; cp "$job" "$top/out/probe-$node.com"; }
$v "$node" $mode "$top/out/probe-$node.com"
rm -f "$job"
