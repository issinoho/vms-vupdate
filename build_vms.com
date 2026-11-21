$! build_vms.com - build VUPDATE on OpenVMS (x86-64 or Alpha), always from clean.
$!   @build_vms            build the core library [.build]vucore.olb
$!   @build_vms TEST       also build and run the unit tests (TEST-OK)
$!   @build_vms PROBE      also link and run probes/curl_probe (PROBE-OK)
$!   @build_vms IMAGE      also build [.build]vupdate.exe (IMAGE-OK); dev_setup.com
$!                         then gives this process a VUPDATE verb that runs it
$! P2 (PROBE only): the CA bundle file for the probe.
$! OpenSSL headers: the VSI SSL3 kit (SSL3$INCLUDE), via the logical OPENSSL.
$! libcurl (D3): vms-curl's static CURLLIB.OLB with its zlib and zstd. Define
$! before PROBE and IMAGE (tools/env-<node>.com does it): VU_CURL_OLB (the library file),
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
$ zinc = ""
$ if f$trnlnm("ZLIB$ROOT") .nes. "" then zinc = ",ZLIB$ROOT:[INCLUDE]"
$ cflags = "/STANDARD=C99" + base + "/INCLUDE=([.src.core],[.src.third.cjson],[.src.app]''zinc')"
$ if f$trnlnm("SSL3$INCLUDE") .eqs. ""
$ then
$   write sys$output "build: SSL3$INCLUDE is not defined (VSI OpenSSL 3 kit)"
$   goto failed
$ endif
$ define/nolog/process openssl SSL3$INCLUDE:
$ open/write opt [.build]ssl.opt
$ write opt "sys$share:ssl3$libssl_shr32.exe/share"
$ write opt "sys$share:ssl3$libcrypto_shr32.exe/share"
$ close opt
$ cjflags = "/STANDARD=RELAXED" + base + "/WARNINGS=DISABLE=ALL"
$ cc'cjflags'/object=[.build]cjson.obj [.src.third.cjson]cjson.c
$ if $severity .ne. 1 then goto failed
$ core = "vu_ver,vu_pcsi,vu_cat,vu_plan,vu_auth,vu_report"
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
$ if p1 .eqs. "IMAGE" then goto image
$ exit 1
$!
$ test:
$ cc'cflags'/object=[.build]test_core.obj [.tests.unit]test_core.c
$ if $severity .ne. 1 then goto failed
$ link/executable=[.build]test_core.exe [.build]test_core.obj,[.build]vucore.olb/library
$ if $severity .ne. 1 then goto failed
$ run [.build]test_core
$ if .not. $status then goto failed
$ cc'cflags'/object=[.build]vu_store.obj [.src.app]vu_store.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_store.obj [.tests.unit]test_store.c
$ if $severity .ne. 1 then goto failed
$ link/executable=[.build]test_store.exe [.build]test_store.obj,vu_store.obj,-
     [.build]vucore.olb/library,[.build]ssl.opt/options
$ if $severity .ne. 1 then goto failed
$ run [.build]test_store
$ if .not. $status then goto failed
$ if f$trnlnm("ZLIB$ROOT") .eqs. ""
$ then
$   write sys$output "build: define ZLIB$ROOT first (tools/env-<node>.com)"
$   goto failed
$ endif
$ cc'cflags'/object=[.build]vu_kit.obj [.src.app]vu_kit.c
$ if $severity .ne. 1 then goto failed
$ cc'cflags'/object=[.build]test_kit.obj [.tests.unit]test_kit.c
$ if $severity .ne. 1 then goto failed
$! Sticky defaults: the ZLIB$ROOT library last, or [.build]ssl.opt is looked for there.
$ link/executable=[.build]test_kit.exe [.build]test_kit.obj,[.build]vu_kit.obj,-
     [.build]ssl.opt/options,ZLIB$ROOT:[LIB]LIBZ.OLB/library
$ if $severity .ne. 1 then goto failed
$ run [.build]test_kit
$ if .not. $status then goto failed
$ write sys$output "TEST-OK"
$ exit 1
$!
$ probe:
$ gosub curl_setup
$ if .not. curl_ok then goto failed
$ cc'base'/STANDARD=C99/object=[.build]curl_probe.obj [.probes]curl_probe.c
$ if $severity .ne. 1 then goto failed
$ link_exe = "[.build]curl_probe.exe"
$ link_objs = "[.build]curl_probe.obj"
$ gosub link_curl
$ if .not. link_ok then goto failed
$ probe = "$" + f$parse("[.build]curl_probe.exe")
$ probe 'p2'
$ exit 1
$!
$ image:
$ gosub curl_setup
$ if .not. curl_ok then goto failed
$ app = "vu_store,vu_http,vu_portal,vu_kit"
$ objs = ""
$ i = 0
$ app_loop:
$   m = f$element(i, ",", app)
$   if m .eqs. "," then goto app_done
$   cc'cflags'/object=[.build]'m'.obj [.src.app]'m'.c
$   if $severity .ne. 1 then goto failed
$   objs = objs + ",[.build]" + m + ".obj"
$   i = i + 1
$   goto app_loop
$ app_done:
$ cc'cflags'/object=[.build]vupdate.obj [.src.plat.vms]vupdate.c
$ if $severity .ne. 1 then goto failed
$ message/object=[.build]vupdate_msg.obj [.src.plat.vms]vupdate_msg.msg
$ if $severity .ne. 1 then goto failed
$! The CLD must compile (into a private copy of the tables: DCLTABLES is untouched).
$ set command/tables=sys$library:dcltables/output=[.build]dcltables_check.exe -
     [.src.plat.vms]vupdate.cld
$ if $severity .ne. 1 then goto failed
$ delete/nolog [.build]dcltables_check.exe;*
$ link_exe = "[.build]vupdate.exe"
$ link_objs = "[.build]vupdate.obj,[.build]vupdate_msg.obj" + objs + ",[.build]vucore.olb/library"
$ gosub link_curl
$ if .not. link_ok then goto failed
$ write sys$output "IMAGE-OK [.build]vupdate.exe (''arch')"
$ exit 1
$!
$! curl_setup: check the library logicals and write [.build]curl.opt.
$ curl_setup:
$ curl_ok = 0
$ olb = f$trnlnm("VU_CURL_OLB")
$ inc = f$trnlnm("VU_CURL_INCLUDE")
$ if olb .eqs. "" .or. inc .eqs. "" .or. f$trnlnm("ZLIB$ROOT") .eqs. "" .or. -
     f$trnlnm("ZSTD$ROOT") .eqs. ""
$ then
$   write sys$output "build: define VU_CURL_OLB, VU_CURL_INCLUDE, ZLIB$ROOT, ZSTD$ROOT first"
$   return
$ endif
$! VSI C opens <curl/curl.h> as CURL:CURL.H, so the logical CURL names that directory.
$ define/nolog/process curl 'inc'
$ open/write opt [.build]curl.opt
$ write opt olb, "/library"
$ write opt "ZLIB$ROOT:[LIB]LIBZ.OLB/LIBRARY"
$ write opt "ZSTD$ROOT:[LIB]LIBZSTD.OLB/LIBRARY"
$ write opt "sys$share:ssl3$libssl_shr32.exe/share"
$ write opt "sys$share:ssl3$libcrypto_shr32.exe/share"
$ close opt
$ curl_ok = 1
$ return
$!
$! link_curl: link link_objs into link_exe against libcurl. vms-curl's CURLLIB.OLB carries
$! %LINK-W-WRNERS (compile warnings in curl's own modules), which curl's build
$! accepts too; anything else (undefined symbols, errors) fails.
$ link_curl:
$ link_ok = 0
$ define/user sys$output [.build]link.log
$ define/user sys$error [.build]link.log
$ link/executable='link_exe' 'link_objs',[.build]curl.opt/options
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
