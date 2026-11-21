#!/usr/bin/env python3
"""ptydrive.py <node> <script> - run DCL in an interactive ssh pty session on a node.
Script lines: a DCL command to send at the prompt, or "?<regex>=<text>" to wait for
<regex> and answer <text>. Used for prompts that need a terminal (VUPDATE LOGIN).
Never put a real password in a script: tests use made-up accounts."""
import os, re, sys
import pexpect

here = os.path.dirname(os.path.abspath(__file__))
node, script = sys.argv[1], sys.argv[2]
row = next(l.split() for l in open(os.path.join(here, "nodes.conf")) if l.split()[:1] == [node])
_, arch, host, port, user, workdir = row[:6]
key = os.environ.get("VMS_SSH_KEY", os.path.expanduser("~/.ssh/vms_ed25519"))
c = pexpect.spawn(f"ssh -tt -i {key} -o BatchMode=yes -p {port} {user}@{host}",
                  encoding="latin-1", timeout=120)
c.logfile_read = sys.stdout
P = r"[\r\n]\x00?[A-Za-z0-9_$]+:: ?\x00?$|[\r\n]\$ ?$"
c.expect(P)
c.send("set terminal/width=132\r"); c.expect(P)
c.send(f"set default {workdir}\r"); c.expect(P)
ready = True  # at a DCL prompt
for line in open(script).read().splitlines():
    if not line.strip() or line.startswith("#"):
        continue
    if line.startswith("?"):
        rx, ans = line[1:].split("=", 1)
        c.expect(rx)
        c.send(ans + "\r")
        continue
    if not ready:
        c.expect(P, timeout=600)
    c.send(line + "\r")
    ready = False
if not ready:
    c.expect(P, timeout=600)
c.send("logout\r")
c.expect([pexpect.EOF, pexpect.TIMEOUT], timeout=15)
