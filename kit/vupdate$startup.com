$! VUPDATE$STARTUP.COM - system startup for VUPDATE (VSI kit updater)
$!
$! Installed into SYS$STARTUP. Defines the system logical name VUPDATE$ROOT (the
$! installed [VUPDATE] directory, concealed). To define it at every boot, add to
$! SYS$MANAGER:SYSTARTUP_VMS.COM:
$!
$!     $ @SYS$STARTUP:VUPDATE$STARTUP.COM
$!
$! P1 = "INSTALL": also protect [DATA] and print the post-installation tasks.
$! P1 = "REMOVE":  delete credentials, cache and kept kits, stop the nightly job
$!                 of this user, deassign VUPDATE$ROOT (PCSI runs it so at removal).
$!
$ set noon
$ mode = f$edit(p1, "UPCASE")
$ proc = f$environment("PROCEDURE")
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$edit(f$parse(proc,,,"DIRECTORY","NO_CONCEAL"), "UPCASE") - "]["
$ root = dir - "SYS$STARTUP]" + "VUPDATE.]"
$ if root .eqs. dir + "VUPDATE.]"
$ then
$   write sys$error "VUPDATE$STARTUP: expected to be in a [SYS$STARTUP] directory, not ''dir'"
$   exit 44
$ endif
$ root = root - ".000000"
$ if mode .eqs. "REMOVE"
$ then
$   if f$trnlnm("VUPDATE$ROOT", "LNM$SYSTEM_TABLE") .eqs. "" then -
        define/system/executive_mode/translation_attributes=concealed VUPDATE$ROOT 'dev''root'
$   if f$search("VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY.COM") .nes. "" then -
        @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY STOP
$   if f$search("VUPDATE$ROOT:[DATA]CREDENTIALS.DAT") .nes. "" then -
        delete/nolog VUPDATE$ROOT:[DATA]CREDENTIALS.DAT;*
$   if f$search("VUPDATE$ROOT:[DATA]*.LIS") .nes. "" then delete/nolog VUPDATE$ROOT:[DATA]*.LIS;*
$   if f$search("VUPDATE$ROOT:[DATA]*.LOG") .nes. "" then delete/nolog VUPDATE$ROOT:[DATA]*.LOG;*
$   if f$search("VUPDATE$ROOT:[CACHE]*.*") .nes. "" then delete/nolog VUPDATE$ROOT:[CACHE]*.*;*
$   if f$search("VUPDATE$ROOT:[KITS.*]*.*") .nes. "" then delete/nolog VUPDATE$ROOT:[KITS.*]*.*;*
$   if f$search("VUPDATE$ROOT:[KITS]*.DIR") .nes. ""
$   then
$     set security/protection=(o:rwed) VUPDATE$ROOT:[KITS]*.DIR;*
$     delete/nolog VUPDATE$ROOT:[KITS]*.DIR;*
$   endif
$   deassign/system/executive_mode VUPDATE$ROOT
$   exit 1
$ endif
$ define/system/executive_mode/translation_attributes=concealed VUPDATE$ROOT 'dev''root'
$ if f$search("VUPDATE$ROOT:[SYSEXE]VUPDATE.EXE") .eqs. ""
$ then
$   write sys$error "VUPDATE$STARTUP: VUPDATE.EXE not found under ''dev'''root'"
$   exit 44
$ endif
$ if mode .nes. "INSTALL" then exit 1
$! Credentials, cache and kits: owner (and system) only.
$ set security/protection=(s:rwe,o:rwe,g,w) VUPDATE$ROOT:[000000]DATA.DIR
$ set security/protection=(s:rwe,o:rwe,g,w) VUPDATE$ROOT:[000000]CACHE.DIR
$ set security/protection=(s:rwe,o:rwe,g,w) VUPDATE$ROOT:[000000]KITS.DIR
$ say = "write sys$output"
$ say ""
$ say "    Post-installation tasks for VUPDATE"
$ say ""
$ say "    At system startup: to define VUPDATE$ROOT at every boot, add to"
$ say "    SYS$MANAGER:SYSTARTUP_VMS.COM:"
$ say "    $ @SYS$STARTUP:VUPDATE$STARTUP.COM"
$ say "    For system managers, the VUPDATE command, in their LOGIN.COM:"
$ say "    $ @VUPDATE$ROOT:[COM]VUPDATE$SETUP.COM"
$ say "    Then, once on this node:  $ VUPDATE LOGIN"
$ say "    Optionally, a nightly check (reports, never installs):"
$ say "    $ @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY START"
$ say ""
$ say "    VUPDATE is for system managers: installing kits needs the privileges of"
$ say "    PRODUCT INSTALL. VUPDATE HELP explains the commands."
$ say ""
$ exit 1
