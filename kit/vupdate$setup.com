$! VUPDATE$SETUP.COM - define the VUPDATE command for this process
$!
$! Add to a system manager's LOGIN.COM:
$!     $ @VUPDATE$ROOT:[COM]VUPDATE$SETUP.COM
$! (The verb goes into the process's command table only; DCLTABLES is untouched.)
$!
$ if f$trnlnm("VUPDATE$ROOT") .eqs. ""
$ then
$   write sys$error "VUPDATE$SETUP: VUPDATE$ROOT is not defined; run VUPDATE$STARTUP.COM first"
$   exit 44
$ endif
$ set command VUPDATE$ROOT:[COM]VUPDATE.CLD
$ exit $status
