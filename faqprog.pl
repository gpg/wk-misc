#!/usr/bin/env perl
#
# $Id: faqprog.pl,v 1.1 2005-02-15 13:33:35 werner Exp $
#
# Convert faq format to postable FAQ/HTML FAQ/or search the FAQ.
#
# A FAQ looks like this:
# <C>	- will be subsituted by new contents.
# <S>	- will be replace by section number.
# <Q>	- will be replaced by section.sub.
# <D>	- defines a symbolic reference to the next question/section
# <R>	- resolves a symbolic reference
# <K>	- defines keywords for the next question
# <s>	- will be replaced by subsection counter
# \[H\s*([^]]*)\] - will be replaced by <$1> (HTML tag)
# \[\$var=value\] - define (possible multiline) variable, only allowed at start
# \[\$var\] - use variable, if defined.
#
# Written for the Solaris 2 FAQ by Casper.Dik@Holland.Sun.COM
#
# Copyright (c) 1994-1996, 1998, 2000 by Casper Dik.
# All rights reserved.
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. All advertising materials mentioning features or use of this software
#    must display the following acknowledgement:
#      This product includes software developed by Casper Dik.
# 
# THIS SOFTWARE IS PROVIDED BY THE CASPER DIK ``AS IS'' AND ANY EXPRESS
# OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED.  IN NO EVENT SHALL CASPER DIK BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
# 

require "getopts.pl";
umask (022);

$opt_h = !!($0 =~ /html/);
$opt_s = !!($0 =~ /sfaq/);
$opt_l = 1;
$opt_m = 5;

if (!&Getopts('VSf:m:hsl:') || ($#ARGV < 0 && $opt_s)) {
    print STDERR "Usage: $0 [-S] [-h] [-f faq] [output]\n";
    print STDERR "Usage: $0 -s [-m max] [-l 0|1|2] [-f faq] expr ...\n";
    exit 255;
}

if ( $opt_V ) {
    print "faqprog.pl 1.0\n";
    exit 0;
}

$faq = $opt_f || $ENV{'FAQSOURCE'} || die "$0: no FAQ specified\n";
$opt_f = "keep perl -w happy";
$tmpf = "/tmp/convert.$$";
$trailer = "</body></html>\n";
open(SRC, "<$faq") || die;

$Q = "#q"; $E = "";
if ($opt_s) {
    $maxmatch = $opt_m;
} else {
    open(TMP, ">$tmpf") || die;
    if ($#ARGV == 0) {
	$out = shift;
	if ($opt_S) {
	    die "Can't make $out" if (!mkdir($out, 0755) && ! -d $out);
	    $Q = "Q";
	    $E = ".html";
	    open(POST, ">$out/index.html") || die;
	} else {
	    open(POST, ">$out") || die;
	}
    } else {
	$out = "(stdout)";
	die "-S requires output name\n" if defined($opt_S);
	open(POST, ">&STDOUT") || die;
    }
}

$xref = "$faq.xref";

if (-f "$xref") {
    open(XREF,"<$xref");
    while(<XREF>) {
	m/(.*)\0(.*)/;
	$ref{$1} = $2;
    }
    close(XREF);
}

print STDERR "Converting $faq to $out",
	$opt_h ? $opt_S ? " (split-html)" : " (html)" : "" , "\n"
    unless($opt_s);

$section = 0;
$question = 0;

$file = "POST" unless ($opt_s);

#
# Read initial variable definitions.
#
while (<SRC>) {
    if (/^\[\$([a-zA-Z]+)=([^]]*)(\])?/) {
	if (!defined($3) || $3 ne "]") {
	    $_ .= <SRC>;
	    redo;
	}
	$var = $1;
	$value = $2;
	$value =~ s/^\n// if ($value =~ /^\n/);
	$vars{$var} = $value;
    } else {
	last;
    }
}

if ($opt_s) {
    while(<SRC>) {
	last if (/<S>/);
    }
    $section = 1;
    for (@ARGV) { $_ = "\Q$_\E" ; s/\s+/\s+/g ; }
    $expr = "(" . join(")|(",@ARGV) . ")";

    $prints = 0;
    $printcurrent = 0;
    @qrefs = ();
    $qtext = "";
} elsif ($opt_h) {
    print POST &mktitle();
} else {
    print POST &getvar('usenetheader'), "\n";
}

$in_q = 0;
$in_pre = 0;

#
# <IF expr> ..[<ELSE>|<ELSIF expr>].. <FI> (not yet implemented)
#
#@if_list = ();
#$showoutput = 1;

main: while (<SRC>) {
    if (/\[\$([a-zA-Z]+)\s*\]/) {
	$_ = $` . &getvar($1) . $';
	redo;
    }
    # Remove http stuff.
    if ((($slash,$tag) = m:^\s*\[H\s*(/)?([^]]*)\]\s*$:)) {
	$in_pre = !defined($slash) if ($tag eq "pre");
	next unless ($opt_h);
    }
    if (!$opt_h) { 
	$url = $' if (s/\[H\s*([aA][^]]+)\]/\001/ && $1 =~ /href=/i && ! /(:\/|ftp|http)/);
	if (defined $url) {
	    if (length($_) + length($url) < 75) {
		chomp;
	    } else {
		$_ .= "\t   ";
	    }
	    $_ .= " <$url>\n";
	    undef $url;
	}
	s/\001//;
	s:\[H\s*/?B\]:*:g;
	s/\[H\s*[^]]*\]//og;
	s/^\s+$/\n/;
    }
    if (/^<C>/) {
	next if ($opt_s);
	print POST "<menu>\n" if ($opt_h);
	$file = "TMP";
    } elsif (/^<D([^>]*)>/) {
	$newref = $1;
	next;
    } elsif (/^<K([^>]*)>/) {
	$newkey = $1;
	next;
    } elsif (/<R([^>]*)>/) {
	# Replace <Rref> w/ $ref{ref})
	$thisref = &get_ref($1);
	local($pre,$post) = ($`, $');
	push(@qrefs,$thisref) if ($opt_s);
	$thisref = "[H a HREF=$Q$thisref$E]${thisref}[H/a]"
		if ($opt_h && !($in_q || /<Q>/));
	$_ = $pre.$thisref.$post;
	redo main;
    } elsif ($in_q) {
	if (/^$/) {
	    $in_q = 0;
	    if ($opt_h) {
		if ($opt_s) {
		    $_ = "</h3>\n";
		} else {
		    print TMP "</h3>\n";
		    print POST "</a>\n";
		}
	    }
	    redo main unless $opt_s;
	}
	if (!$opt_s) {
	    &htmlize($_) if $opt_h;
	    print TMP $_;
	    s/^\s*/	/ unless ($opt_h);
	    print POST $_;
	}
    } elsif (/<Q>/) {
	$subsection = 0;
	$question++;
	$in_q = 1;
	$ref = "$section.$question";
	&mkref($ref,"Question");
	$_ = $';
	&htmlize($_) if $opt_h;
	if ($opt_s) {
	    &store_q;
	    if ($opt_h) {
		$_ = "<h3>\n<a NAME=q$ref>$ref)</a>$_";
	    } else {
		$_ = "$`$ref)$_";
	    }
	    $kwmatch = defined($newkey);
	    $kwmatch ++ if ($kwmatch && "$newkey $ref" =~ m/$expr/io);
	    undef $newkey;
	    #print STDERR $_;
	} elsif ($opt_h) {
	    print TMP "<h3>\n<a NAME=q$ref>$ref)</a>$_";
	    print POST "<LI><a HREF=$Q$ref$E>$ref)$_";
	} else {
	    $tmp = $`;
	    $tmp = " " if (length($tmp) == 0);
	    print TMP "$`$ref)$'";
	    print POST "  $tmp$ref)$'";
	}
    } elsif (/<S>/) {
	$section++;
	$line = "$section.$'";
	&mkref($section,"Section");
	&htmlize($line) if ($opt_h);
	if ($opt_s) {
	    &store_q;
	} elsif ($opt_h) {
	    print TMP "<h2>\n<A NAME=q$section>$line</A></h2>\n";
	    print POST "<h2><A HREF=$Q$section$E>$line</A></h2>\n";
	} else {
	    $tmp = $`;
	    $tmp = " " if (length($tmp) == 0);
	    print TMP $line;
	    print POST "\n$tmp$line";
	}
	$subsection = 0;
	$question = 0;
    } elsif (/<s>/) {
	$subsection++;
	$_ = $`. ($last ne "<P>\n" && $opt_h ? '[H BR]' : "") . "$subsection)$'";
	&htmlize($_) if $opt_h;
	print TMP $_ unless ($opt_s);
    } else {
	if ($opt_h) {
	    $_ = $' if (/^    /);
	    &htmlize($_);
	}
	print $file $_ unless ($opt_s);
    }
    if ($opt_s) {
	if (!$printcurrent && ($kwmatch == 2 || !$kwmatch && /$expr/io)) {
	    $prints ++;
	    die "Too many matching questions\n"
		if ($maxmatch > 0 && $prints > $maxmatch);
	    $printcurrent = 1;
	}
	$qtext .= $_;
    }
    $last = $_;
}

if ($opt_s) {
    if ($prints) {
	$output = ""; $cheat = 0; $mods = 0;
	foreach $q (sort sortq keys(%qprint)) {
	    $cheat ++ if ($qtext{$q} =~ /^\+/);
	    $mods ++ if ($qtext{$q} =~ /^\*/);
	    $output .= $qtext{$q};
	}
	if ($cheat) {
	    print "The FAQ maintainer cheated and added this to the FAQ:\n\n";
	} else {
	    print &getvar('sfaqheader'), "\n\n";
	}
	print $output;
	print "    --- end of excerpt from the FAQ\n\n";
	print
	    "Questions marked with a * or + have been changed or added since\n",
	    "the FAQ was last posted\n\n" if ($mods || $cheat);
	print
	    &getvar('sfaqfooter') unless ($opt_h);
    } else {
	print "No matching questions\n";
    }
} else {
    &mkref();
    print POST "</menu>\n" if ($opt_h);
    unless ($opt_S) {
	open(TMP, "<$tmpf") || die;
	print POST $_ while <TMP>;
    }
    close(TMP);
    unlink "$tmpf";
    print POST $trailer if ($opt_h);
}
    if (defined($refchanged)) {
	print STDERR "$0: writing $xref\n";
	open(XREF,">$xref");
	foreach $k (keys(%newref)) {
	    print XREF "$k\0$newref{$k}\n";
	}
	close(XREF);
	exit 1;
    }
exit 0;

#
# Do two levels of references only.
#
sub store_q {
    #print $qtext if ($printcurrent);
 
    if (defined($lastq)) {
	$qtext{$lastq} = $qtext;
	$qrefs{$lastq} = join(":",@qrefs);
	if ($printcurrent) {
	    $qprint{$lastq} = 1;
	    if ($opt_l >= 1) {
		foreach $r (@qrefs) {
		    $qprint{$r} = 1;
		    if ($opt_l >= 2 && defined($qrefs{$r})) {
			foreach $r2 (split(':',$qrefs{$r})) {
			    $qprint{$r2} = 1;
			}
		    }
		}
	    }
	}
    }

    if (defined($lastq) && $lastq eq $ref) {
	undef $lastq;
    } else {
	$lastq = $ref if (defined($ref));
    }

    $printcurrent = 0;
    $qtext = "";
    @qrefs = ();
}

sub add_ref {
    local($qref,$ref) = @_;

    if (!defined($ref{$qref}) || $ref{$qref} ne $ref) {
	unless (defined($refchanged)) {
	    warn "$0: references changed, rerun\n";
	    $refchanged = 1;
	}
	$ref{$qref} = $ref;
    }
    $newref{$qref} = $ref;
}

sub get_ref {
    local($qref) = @_;

    if (!defined($ref{$qref})) {
	warn "$0: no reference \"$qref\"\n";
	"error in FAQ: no reference \"$qref\"";
    } else {
	$ref{$qref};
    }
}

sub sortq {
    local(@q1,@q2);

    @q1 = split('\.', $a);
    @q2 = split('\.', $b);

    $q1[0] <=> $q2[0] || $q1[1] <=> $q2[1];
}

sub htmlize {
    if ($_[0] =~ /^$/) {
	$_[0] = "<P>\n" unless ($in_pre);
    } elsif ($_[0] =~ /[&<>"\[]/) {
	if (! $in_pre) {
	    $_[0] =~ s/&/\&amp;/g;
	    $_[0] =~ s/"/\&quot;/g;
	}
	$_[0] =~ s/>/\&gt;/g;
	$_[0] =~ s/</\&lt;/g;
	$_[0] =~ s/\[H\s*([^]]*)\]/<$1>/g;
    }
}

sub getvar {
    local($v) = $vars{$_[0]};
    unless (defined($v)) {
	warn "\$$_[0] not defined in FAQ\n";
	 "FAQ source ERROR: '\$$_[0]' not defined";
    } else {
	$v;
    }
}
sub mktitle {
    local($q) = @_;
    "<html><head>\n<title>" .
    &getvar('htmltitle') . (defined($q) ? " $q" : "") . "</title>\n" .
    &getvar('htmlcharset') . (defined($q) ? " $q" : "") . 
    "\n</head>\n<body>\n";
}

sub mkref {
    local($next, $title) = @_;

    if (!$opt_S) {
	if (defined($newref) && defined($next)) {
	    &add_ref($newref,$next);
	    undef $newref;
	}
	return;
    }
    if (defined($PrevQ)) {
	print TMP "<A HREF=$Q$PrevQ$E>PREV</A>\n";
    }
    print TMP "<A HREF=index.html>INDEX</A>\n";
    if (defined($next)) {
	print TMP "<A HREF=$Q$next$E>NEXT</A>\n";
	$PrevQ = $CurQ if (defined $CurQ);
	$CurQ = $next;
    }
    print TMP $trailer;
    if (defined($next)) {
	open(TMP,">$out/$Q$next$E");
	print TMP &mktitle("$title $next");
    }
}

