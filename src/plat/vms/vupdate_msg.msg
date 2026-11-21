! vupdate_msg.msg - VUPDATE messages. The condition values are also computed in
! vupdate.c (VU_MSG): keep the order of the messages and the facility number in step.
        .TITLE          VUPDATE messages
        .FACILITY       VUPDATE,1903 /PREFIX=VU__
        .BASE           1

        .SEVERITY       SUCCESS
CURRENT     <all !UL VSI product!%S checked are up to date>/FAO=1
LOGGEDIN    <signed in as !AZ; credentials saved in !AZ>/FAO=2
LOGGEDOUT   <stored credentials deleted (!AZ)>/FAO=1

        .SEVERITY       INFORMATIONAL
UPDATES     <!UL update!%S available>/FAO=1
NOTSAVED    <no stored credentials (!AZ)>/FAO=1

        .SEVERITY       WARNING
NYI         <!AZ is not implemented yet>/FAO=1

        .SEVERITY       ERROR
NOCRED      <not signed in: use VUPDATE LOGIN first (!AZ)>/FAO=1
AUTHFAIL    <sign-in failed: !AZ>/FAO=1
NETERR      <cannot reach the VSI portal: !AZ>/FAO=1
PORTAL      <the VSI portal returned an error: !AZ>/FAO=1
BADREPLY    <unexpected reply from the VSI portal (has its API changed?): !AZ>/FAO=1
STORE       <credential store !AZ: !AZ>/FAO=2
PCSI        <cannot read the PCSI product list: !AZ>/FAO=1
NOTERM      <!AZ needs a terminal>/FAO=1
FAILED      <!AZ>/FAO=1

! Phase 2. New messages go at the end only: the numbers are positional.
        .SEVERITY       SUCCESS
DOWNLOADED  <!UL kit!%S downloaded and checked in !AZ>/FAO=2
INSTALLED   <!UL kit!%S installed>/FAO=1
        .SEVERITY       INFORMATIONAL
NOTHING     <nothing to upgrade>
        .SEVERITY       WARNING
NOTFOUND    <!AZ: !AZ>/FAO=2
REBOOT      <!AZ needs a reboot: reboot before installing more kits>/FAO=1
        .SEVERITY       ERROR
MD5         <!AZ: MD5 mismatch, expected !AZ>/FAO=2
KIT         <!AZ: !AZ>/FAO=2
ABORTED     <!AZ>/FAO=1

        .END
