#!/usr/bin/env bash
# image.sh <node> [dcl-file] - push, run the unit tests, build [.build]vupdate.exe,
# set up the process VUPDATE verb (dev_setup.com) and then run the DCL lines in
# dcl-file (default: the credential-free checks below). Prints TEST-OK, IMAGE-OK.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: image.sh <node> [dcl-file]}
v=$top/tools/vms.sh
cd "$top"
tools/push.sh "$node" >/dev/null
$v "$node" put "tools/env-$node.com" ~/projects/vms-curl/cache/cacert-2026-09-25.pem -- build_ca
read -r _ ARCH _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
mkdir -p "$top/out"
job=$top/out/image-$node.com
cat > "$job" <<DCL
\$ set noon
\$ set default $WORKDIR
\$ @[.build_ca]env-$node.com
\$ @build_vms TEST
\$ if .not. \$status then exit \$status
\$ @build_vms IMAGE
\$ if .not. \$status then exit \$status
\$ @dev_setup [.build_ca]cacert-2026-09-25.pem
DCL
if [ $# -ge 2 ]; then
    cat "$2" >> "$job"
else
    # The credential-free checks use a throwaway store path, never the real one
    # (a LOGOUT here once deleted the user's saved sign-in, 2026-11-21).
    cat >> "$job" <<'DCL'
$ define/process VUPDATE$CREDENTIALS VUPDATE$ROOT:[DATA]NO_SUCH_TEST_STORE.DAT
$ write sys$output "== LOGIN without a terminal =="
$ vupdate login/username="vupdate-test@example.invalid"
$ write sys$output "status ", $status
$ write sys$output "== LOGOUT, nothing stored =="
$ vupdate logout
$ write sys$output "status ", $status
$ write sys$output "== CHECK, not signed in =="
$ vupdate check
$ write sys$output "status ", $status
$ deassign/process VUPDATE$CREDENTIALS
DCL
fi
mode=run; [ "$ARCH" = ALPHA ] && mode=batch
$v "$node" $mode "$job"
