$! dev_setup.com - give this process a VUPDATE verb running [.build]vupdate.exe,
$! for development and tests. Process-wide only: no system logicals, DCLTABLES
$! untouched. Run @build_vms IMAGE first.
$!   @dev_setup [ca-bundle-file]
$! VUPDATE$ROOT becomes [.DEVROOT.] beside this procedure: [SYSEXE] the image,
$! [DATA] the credentials and CA bundle (owner access only), [CACHE] replies.
$ set noon
$ proc = f$environment("PROCEDURE")
$! NO_CONCEAL: a concealed rooted logical can't sit on another rooted one (N12).
$ dev = f$parse(proc,,,"DEVICE","NO_CONCEAL")
$ dir = f$parse(proc,,,"DIRECTORY","NO_CONCEAL") - "][" - "]"
$ root = dev + dir + ".DEVROOT.]"
$ here = f$parse(proc,,,"DEVICE") + f$parse(proc,,,"DIRECTORY")
$ bld = here - "]" + ".BUILD]"
$ cld = here - "]" + ".SRC.PLAT.VMS]VUPDATE.CLD"
$ if f$search(bld + "VUPDATE.EXE") .eqs. ""
$ then
$   write sys$output "dev_setup: no [.BUILD]VUPDATE.EXE: run @build_vms IMAGE first"
$   exit 44
$ endif
$ define/nolog/process/translation_attributes=concealed VUPDATE$ROOT 'root'
$ if f$parse("VUPDATE$ROOT:[SYSEXE]") .eqs. "" then create/directory VUPDATE$ROOT:[SYSEXE]
$ if f$parse("VUPDATE$ROOT:[DATA]") .eqs. "" then -
     create/directory/protection=(S:RWE,O:RWE,G,W) VUPDATE$ROOT:[DATA]
$ if f$parse("VUPDATE$ROOT:[CACHE]") .eqs. "" then create/directory VUPDATE$ROOT:[CACHE]
$ if f$parse("VUPDATE$ROOT:[HELP]") .eqs. "" then create/directory VUPDATE$ROOT:[HELP]
$ if f$parse("VUPDATE$ROOT:[COM]") .eqs. "" then create/directory VUPDATE$ROOT:[COM]
$ copy/nolog 'cld' VUPDATE$ROOT:[COM]VUPDATE.CLD
$ kitdir = here - "]" + ".KIT]"
$ if f$search(kitdir + "VUPDATE_NIGHTLY.COM") .nes. "" then -
     copy/nolog 'kitdir'VUPDATE_NIGHTLY.COM VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY.COM
$ purge/nolog VUPDATE$ROOT:[COM]
$ copy/nolog 'bld'VUPDATE.HLB VUPDATE$ROOT:[HELP]VUPDATE.HLB
$ purge/nolog VUPDATE$ROOT:[HELP]VUPDATE.HLB
$ copy/nolog 'bld'VUPDATE.EXE VUPDATE$ROOT:[SYSEXE]VUPDATE.EXE
$ purge/nolog VUPDATE$ROOT:[SYSEXE]VUPDATE.EXE
$ if p1 .nes. ""
$ then
$   copy/nolog 'p1' VUPDATE$ROOT:[DATA]CACERT.PEM
$   purge/nolog VUPDATE$ROOT:[DATA]CACERT.PEM
$ endif
$ if f$search("VUPDATE$ROOT:[DATA]CACERT.PEM") .eqs. "" then -
     write sys$output "dev_setup: warning: no VUPDATE$ROOT:[DATA]CACERT.PEM (give the CA bundle as P1)"
$ set command 'cld'
$ if .not. $status then exit $status
$ write sys$output "dev_setup: VUPDATE verb ready; VUPDATE$ROOT = ", f$trnlnm("VUPDATE$ROOT")
$ exit 1
