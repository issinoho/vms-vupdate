$! VUPDATE_NIGHTLY.COM - check the VSI Service Platform every night (never installs)
$!
$!   @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY START [time]   queue it (default 02:00 daily)
$!   @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY STOP           remove the queued job
$!   @VUPDATE$ROOT:[COM]VUPDATE_NIGHTLY NOW            run a check now, interactively
$! The batch job itself runs with P1 = RUN: it queues tomorrow's run first, then runs
$! VUPDATE CHECK under the submitting user (who did VUPDATE LOGIN on this node).
$!
$! When the findings differ from the last run (new updates, patches, security fixes,
$! or an error) it sends an operator message (REQUEST, to CENTRAL) and, when the
$! logical VUPDATE$NOTIFY_MAIL names a mail address, mails the report there.
$! Files, in VUPDATE$ROOT:[DATA]: NIGHTLY.LIS (the last report), NIGHTLY_FINDINGS.LIS
$! (what is compared), VUPDATE_NIGHTLY.LOG (the batch log).
$! Logicals: VUPDATE$NIGHTLY_TIME (hh:mm), VUPDATE$NIGHTLY_QUEUE (default SYS$BATCH).
$!
$ set noon
$ mode = f$edit(p1, "UPCASE,TRIM")
$ if mode .eqs. "" then mode = "NOW"
$ me = f$environment("PROCEDURE")
$ me = f$parse(me,,,"DEVICE") + f$parse(me,,,"DIRECTORY") + f$parse(me,,,"NAME") + -
       f$parse(me,,,"TYPE")
$ jobname = "VUPDATE_NIGHTLY"
$ queue = f$trnlnm("VUPDATE$NIGHTLY_QUEUE")
$ if queue .eqs. "" then queue = "SYS$BATCH"
$ at = p2
$ if at .eqs. "" then at = f$trnlnm("VUPDATE$NIGHTLY_TIME")
$ if at .eqs. "" then at = "02:00"
$ data = "VUPDATE$ROOT:[DATA]"
$ if mode .eqs. "START" then goto start
$ if mode .eqs. "STOP" then goto stop
$ if mode .eqs. "RUN" then goto run
$ if mode .eqs. "NOW" then goto check
$ write sys$error "VUPDATE_NIGHTLY: P1 must be START, STOP, NOW or RUN"
$ exit 44
$!
$ start:
$ gosub remove_jobs
$ after = f$cvtime("TOMORROW+" + at, "ABSOLUTE")
$ if f$cvtime(at) .gts. f$cvtime() then after = f$cvtime(at, "ABSOLUTE")
$ gosub submit
$ if .not. ok then exit 44
$ write sys$output "VUPDATE_NIGHTLY: queued on ''queue' for ''after', then daily at ''at'"
$ exit 1
$!
$ stop:
$ gosub remove_jobs
$ write sys$output "VUPDATE_NIGHTLY: ''removed' queued job(s) removed"
$ exit 1
$!
$ run:
$! Tomorrow's run first, so one failed check doesn't end the series.
$ gosub remove_jobs
$ after = f$cvtime("TOMORROW+" + at, "ABSOLUTE")
$ gosub submit
$ check:
$ set command VUPDATE$ROOT:[COM]VUPDATE.CLD
$ define/user sys$output 'data'NIGHTLY_MSG.LIS
$ define/user sys$error 'data'NIGHTLY_MSG.LIS
$ vupdate check/output='data'NIGHTLY.LIS
$ st = $status
$ sev = st .and. 7
$ bad = sev .eq. 0 .or. sev .eq. 2 .or. sev .eq. 4  ! warning, error, fatal
$! The findings: report lines minus the two header lines (date, cache age), and the
$! message when the check failed.
$ open/write f 'data'NIGHTLY_NEW.LIS
$ nupd = 0
$ nsec = 0
$ if f$search("''data'NIGHTLY.LIS") .eqs. "" .or. bad then goto failed
$ open/read r 'data'NIGHTLY.LIS
$ rloop:
$ read/end=rdone r line
$ if f$locate("VUPDATE CHECK on", line) .eq. 0 then goto rloop
$ if f$locate("VSI catalogue:", line) .eq. 0 then goto rloop
$ if f$locate("/ALL also lists", line) .lt. f$length(line) then goto rloop
$ write f line
$ if f$extract(0, 2, line) .eqs. "  " then nupd = nupd + 1
$ if f$locate("security", line) .lt. f$length(line) then nsec = nsec + 1
$ goto rloop
$ rdone:
$ close r
$ goto compare
$ failed:
$ open/read r 'data'NIGHTLY_MSG.LIS
$ read/end=fdone r line
$ write f line
$ fdone:
$ close r
$ compare:
$ close f
$ checksum 'data'NIGHTLY_NEW.LIS
$ new = checksum$checksum
$ old = ""
$ if f$search("''data'NIGHTLY_FINDINGS.LIS") .nes. ""
$ then
$   checksum 'data'NIGHTLY_FINDINGS.LIS
$   old = checksum$checksum
$ endif
$ rename/nolog 'data'NIGHTLY_NEW.LIS 'data'NIGHTLY_FINDINGS.LIS
$ purge/nolog 'data'NIGHTLY_FINDINGS.LIS
$ if f$search("''data'NIGHTLY_MSG.LIS") .nes. "" then delete/nolog 'data'NIGHTLY_MSG.LIS;*
$ if f$search("''data'NIGHTLY.LIS;-1") .nes. "" then purge/nolog 'data'NIGHTLY.LIS
$ node = f$edit(f$getsyi("SCSNODE"), "TRIM")
$ if bad
$ then
$   text = "VUPDATE on ''node': the nightly check failed, see ''data'NIGHTLY_FINDINGS.LIS"
$ else
$   if nupd .eq. 0 then text = "VUPDATE on ''node': everything is up to date"
$   if nupd .gt. 0 then text = "VUPDATE on ''node': ''nupd' update(s) or patch(es) available" + -
        ", ''nsec' flagged security; VUPDATE CHECK for details"
$ endif
$ write sys$output text
$ if new .eqs. old
$ then
$   write sys$output "VUPDATE_NIGHTLY: no change since the last check"
$   exit (st .or. %X10000000)
$ endif
$ request "''text'"
$ to = f$trnlnm("VUPDATE$NOTIFY_MAIL")
$ if to .nes. "" .and. f$search("''data'NIGHTLY.LIS") .nes. ""
$ then
$   mail/subject="''text'" 'data'NIGHTLY.LIS "''to'"
$ endif
$ exit (st .or. %X10000000)
$!
$! submit: queue this procedure with P1 = RUN at "after" (sets ok).
$ submit:
$ ok = 0
$ submit/queue='queue'/after="''after'"/name='jobname'/noprinter -
     /log_file='data'VUPDATE_NIGHTLY.LOG/parameters=(RUN,"''at'") 'me'
$ if $status then ok = 1
$ return
$!
$! remove_jobs: delete this user's queued VUPDATE_NIGHTLY jobs (sets removed).
$ remove_jobs:
$ removed = 0
$ junk = f$getqui("CANCEL_OPERATION")
$ qloop:
$ qname = f$getqui("DISPLAY_QUEUE", "QUEUE_NAME", "*", "BATCH,WILDCARD")
$ if qname .eqs. "" then return
$ jloop:
$ jname = f$getqui("DISPLAY_JOB", "JOB_NAME", , "ALL_JOBS")
$ if jname .eqs. "" then goto qloop
$ if jname .nes. jobname then goto jloop
$ owner = f$edit(f$getqui("DISPLAY_JOB", "USERNAME", , "FREEZE_CONTEXT"), "TRIM")
$ if owner .nes. f$edit(f$getjpi("", "USERNAME"), "TRIM") then goto jloop
$ if f$getqui("DISPLAY_JOB", "JOB_EXECUTING", , "FREEZE_CONTEXT") then goto jloop
$ entry = f$getqui("DISPLAY_JOB", "ENTRY_NUMBER", , "FREEZE_CONTEXT")
$ delete/entry='entry'
$ removed = removed + 1
$ goto jloop
