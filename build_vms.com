$! build_vms.com - build VUPDATE on OpenVMS (x86-64 or Alpha), always from clean.
$!   @build_vms            build the core library [.build]vucore.olb
$!   @build_vms TEST       also build and run the unit tests (TEST-OK)
$!   @build_vms PROBE      also link and run probes/curl_probe (PROBE-OK)
$! P2 (PROBE only): the CA bundle file for the probe.
$! libcurl (D3): vms-curl's static CURLLIB.OLB with its zlib and zstd. Define
$! before PROBE (tools/env-<node>.com does it): VU_CURL_OLB (the library file),
$! VU_CURL_INCLUDE (the directory holding CURL.H), and the rooted logicals
$! ZLIB$ROOT and ZSTD$ROOT (vms-zlib / vms-zstd install trees, [LIB]*.OLB).
$! Any compiler warning in our code stops the build (as -Werror does on Linux).
$ set noon
$ here = f$parse(f$environment("PROCEDURE"),,,"DEVICE") + f$parse(f$environment("PROCEDURE"),,,"DIRECTORY")
$ set default 'here'
$ arch = f$edit(f$getsyi("ARCH_NAME"), "UPCASE")
$ if f$search("[.build]*.*") .nes. "" then delete/nolog [.build]*.*;*
$ if f$search("build.dir") .eqs. "" then create/directory [.build]
$ float = ""
$ if arch .eqs. "ALPHA" then float = "/FLOAT=IEEE_FLOAT/IEEE_MODE=DENORM_RESULTS"
$! Under /STANDARD=C99 only ANSI names get the DECC$ prefix: ALL_ENTRIES maps the rest.
$ base = "/POINTER_SIZE=32/PREFIX_LIBRARY_ENTRIES=ALL_ENTRIES/NAMES=(AS_IS,SHORTENED)" + float
$ cflags = "/STANDARD=C99" + base + "/INCLUDE=([.src.core],[.src.third.cjson])"
$ cjflags = "/STANDARD=RELAXED" + base + "/WARNINGS=DISABLE=ALL"
$ cc'cjflags'/object=[.build]cjson.obj [.src.third.cjson]cjson.c
$ if $severity .ne. 1 then goto failed
$ core = "vu_ver,vu_pcsi,vu_cat,vu_plan"
$ library/create/object [.build]vucore.olb
$ library/insert [.build]vucore.olb [.build]cjson.obj
$ i = 0
$ core_loop:
$   m = f$element(i, ",", core)
$   if m .eqs. "," then goto core_done
$   cc'cflags'/object=[.build]'m'.obj [.src.core]'m'.c
$   if $severity .ne. 1 then goto failed
$   library/insert [.build]vucore.olb [.build]'m'.obj
$   i = i + 1
$   goto core_loop
$ core_done:
$ write sys$output "build: [.build]vucore.olb (''arch')"
$ if p1 .eqs. "TEST" then goto test
$ if p1 .eqs. "PROBE" then goto probe
$ exit 1
$!
$ test:
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ link/executable=[.build]test_core.exe [.build]test_core.obj,[.build]vucore.olb/library
$ if $severity .ne. 1 then goto failed
$ run [.build]test_core
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$!
$ probe:
$ olb = f$trnlnm("VU_CURL_OLB")
$ inc = f$trnlnm("VU_CURL_INCLUDE")
$ if olb .eqs. "" .or. inc .eqs. "" .or. f$trnlnm("ZLIB$ROOT") .eqs. "" .or. -
     f$trnlnm("ZSTD$ROOT") .eqs. ""
$ then
$   write sys$output "build: define VU_CURL_OLB, VU_CURL_INCLUDE, ZLIB$ROOT, ZSTD$ROOT first"
$   goto failed
$ endif
$! VSI C opens <curl/curl.h> as CURL:CURL.H, so the logical CURL names that directory.
$ define/nolog/process curl 'inc'
$ cc'base'/STANDARD=C99/object=[.build]curl_probe.obj [.probes]curl_probe.c
$ if $severity .ne. 1 then goto failed
$ open/write opt [.build]curl.opt
$ write opt olb, "/library"
$ write opt "ZLIB$ROOT:[LIB]LIBZ.OLB/LIBRARY"
$ write opt "ZSTD$ROOT:[LIB]LIBZSTD.OLB/LIBRARY"
$ write opt "sys$share:ssl3$libssl_shr32.exe/share"
$ write opt "sys$share:ssl3$libcrypto_shr32.exe/share"
$ close opt
$ gosub link_curl
$ if .not. link_ok then goto failed
$ probe = "$" + f$parse("[.build]curl_probe.exe")
$ probe 'p2'
$ exit 1
$!
$! link_curl: link the probe against libcurl. vms-curl's CURLLIB.OLB carries
$! %LINK-W-WRNERS (compile warnings in curl's own modules), which curl's build
$! accepts too; anything else (undefined symbols, errors) fails.
$ link_curl:
$ link_ok = 0
$ define/user sys$output [.build]link.log
$ define/user sys$error [.build]link.log
$ link/executable=[.build]curl_probe.exe [.build]curl_probe.obj,[.build]curl.opt/options
$ sev = $severity
$ type [.build]link.log
$ if sev .eq. 1 then link_ok = 1
$ if sev .ne. 0 then return
$ search/nooutput [.build]link.log "%LINK-","%ILINK-"
$ if $severity .ne. 1 then return
$ search/nooutput [.build]link.log "UDFSYM","USEUNDEF","-E-","-F-" /match=or
$ if $severity .eq. 1 then return
$ link_ok = 1
$ return
$!
$ failed:
$ write sys$output "BUILD-FAILED"
$ exit 44
