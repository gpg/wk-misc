#!/usr/bin/perl

# Modified by wk@gnupg.org so that -m list does only 
#       work with one mail address (usually a ML), set a fixed 
#       From: address so that mailman needs only one allowed poster,
#       Use a default logfile with with not -f arg.             2001-10-18
# Modified by werner.koch@guug.de to add support for
#	automagically extraction of ChangeLog entries		1998-11-27
# Modified by woods@web.apc.org to add support for mailing	3/29/93
#	use '-m user' for each user to receive cvs log reports
#	and use '-f logfile' for the logfile to append to
#
# Modified by berliner@Sun.COM to add support for CVS 1.3	2/27/92
#
# Date: Tue, 6 Aug 91 13:27 EDT
# From: samborn@sunrise.com (Kevin Samborn)
#
# I revised the perl script I sent you yesterday to use the info you
# send in on stdin.  (I am appending the newer script to the end)

$cvsroot = $ENV{'CVSROOT'};
$fromaddr = "cvs\@cvs.gnupg.org";

# turn off setgid
#
$) = $(;

# parse command line arguments
#
while (@ARGV) {
	$arg = shift @ARGV;

	if ($arg eq '-m') {
		($users) && die "Too many '-m' args";
		$users = shift @ARGV;
	} elsif ($arg eq '-f') {
		($logfile) && die "Too many '-f' args";
		$logfile = shift @ARGV;
	} else {
		($donefiles) && die "Too many arguments!\n";
		$donefiles = 1;
		@files = split(/ /, $arg);
	}
}

($logfile) || $logfile = "$cvsroot/CVSROOT/commitlog";

$srepos = shift @files;
$mailcmd = "| /usr/sbin/sendmail -t -oi";

# Some date and time arrays
#
@mos = (January,February,March,April,May,June,July,August,September,
	October,November,December);
@days = (Sunday,Monday,Tuesday,Wednesday,Thursday,Friday,Saturday);

($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime;

$year = $year + 1900;

# get login name
#
$login = getlogin || (getpwuid($<))[0] || "nobody";

# open log file for appending
#
open(OUT, ">>" . $logfile) || die "Could not open(" . $logfile . "): $!\n";
if ($users) {
	open(MAIL, $mailcmd) || die "Could not Exec($mailcmd): $!\n";
}

# print out the log Header
#
print OUT "\n";
print OUT "**************************************\n";
print OUT "Date:\t$days[$wday] $mos[$mon] $mday, $year @ $hour:" . sprintf("%02d", $min) . "\n";
print OUT "Author:\t$login\n\n";

if (MAIL) {
        print MAIL "From: $fromaddr\n";
	print MAIL "To: $users\n";
	print MAIL "Subject: $login committed to $srepos\n";
        print MAIL "\n";
	print MAIL "Date:\t$days[$wday] $mos[$mon] $mday, $year @ $hour:" . sprintf("%02d", $min) . "\n";
	print MAIL "Author:\t$login\n\n";
}

# print the stuff from logmsg that comes in on stdin to the logfile
#
open(IN, "-");
while (<IN>) {
	chop;
	if( /^See[ ]ChangeLog:[ ](.*)/ ) {
	    $changelog = $1;
	    $okay = false;
	    open(RCS, "-|") || exec 'cvs', '-Qn', 'update', '-p', 'ChangeLog';
	    while (<RCS>) {
		if( /^$changelog .*/ ) {
		    $okay = true;
		    print OUT;
		    MAIL && print MAIL;
		}
		elsif( $okay ) {
		    last if( /^[A-Z]+.*/ );
		    print OUT;
		    MAIL && print MAIL;
		}
	    }
	    while (<RCS>) { ; }
	    close(RCS);
	}
	print OUT $_, "\n";
	MAIL && print MAIL $_, "\n";
}
close(IN);

print OUT "\n";



# after log information, do an 'cvs -Qn status' on each file in the arguments.
#
while (@files) {
	$file = shift @files;
	if ($file eq "-") {
		print OUT "[input file was '-']\n";
		if (MAIL) {
			print MAIL "[input file was '-']\n";
		}
		last;
	}

	open(RCS, "-|") || exec 'cvs', '-Qn', 'status', $file;

	while (<RCS>) {
		if (/^[ \t]*Version/ || /^File:/) {
			print OUT;
			if (MAIL) {
				print MAIL;
			}
		}
	}
	close(RCS);
}

close(OUT);
die "Write to $logfile failed" if $?;

close(MAIL);
die "Pipe to $mailcmd failed" if $?;

exit 0;

