#!/bin/sh
#

# Example driver:
##!/bin/sh
#
#hostsfile="$HOME/bin/hosts.sks"
#outfile="/var/www/217.69.76.60/keystats.gnupg.net/htdocs/index.html"
#failedfile="/var/www/217.69.76.60/keystats.gnupg.net/htdocs/failed-hosts"
#
#rm "$failedfile.now" 2>/dev/null || true
#$HOME/bin/sks-stats.sh --failed "$failedfile" --hosts "$hostsfile" >"$outfile".tmp 2>/dev/null
#mv "$outfile".tmp "$outfile"
#if [ -f "$failedfile.now" ]; then
#   rm "$failedfile.now" 2>/dev/null
#   ( echo "List of failed hosts:"
#     cat  "$failedfile" ) |  mail -s "keys.gnupg.net status change detected" keystats-gnupg-net@gnupg.org
#fi
#


function get_stats () {
   local host
   local port

   hostip="$1"    
   host=""
   port="$2"

   if [ -n "$hostsfile" -a -f "$hostsfile" ]; then
      host=$(grep "^$hostip" "$hostsfile" | awk '{print $2; exit}')
   fi
   if [ -z "$host" ]; then
      host="$hostip"
   fi
   echo "retrieving $host:$port using $hostip" >&2

  echo "Server $hostip $port"
  # Note that versions of wget < 1.10 can't override 'Host:'.
  (wget -qO - -T 30 -t 3 --no-cache --header "Host: $host:$port" \
     "http://$hostip:$port/pks/lookup?op=stats" || echo) |\
    awk -v failed="${failed_hosts_file}" -v hostip="$hostip" -v hostn="$host" '
/<\/table>/           {in_settings = 0; in_peers = 0; in_daily = 0}
/<h2>Settings<\/h2>/  {in_settings = 1 }
/<h2>Gossip Peers<\/h2>/ {in_peers = 1 }
/<h2>Statistics<\/h2>/   {in_stats = 1 }

in_settings && /<td>Hostname:/ {split($0, a, /:<td>/); host=a[2] }
in_settings && /<td>Version:/ {split($0, a, /:<td>/);  version=a[2] }

in_peers && /<td>/ {split($0, a , /[ \t\n]+|<td>/);
                    peers[in_peers]=a[2]; in_peers++;  }

!in_stats  {next}

/<h3>Daily Histogram<\/h3>/   {in_daily = 1 }

!nkeys && /<p>Total number of keys:/ { split($0, a, /Total number of keys:/);
                                       split(a[2], aa, /</); nkeys = aa[1]; }

in_daily && /<tr><td>2/ { split($0, a, /<td>/); daily[a[2]] = a[3] " " a[4]; }

END {
  if ( host ) {
    print "Host " host
    print "Version " version
    print "Total  " nkeys
    for ( i in peers ) print "Peer " peers[i];
    for ( i in daily ) { print "Daily " i " " daily[i]  }
  } else {
    print hostip "  " hostn  >> failed
  }
}
'
echo "Timestamp $(date -u '+%F %T')"
}


function print_header2 () {
echo "$1"
echo '<table cellpadding=5 cellspacing=1 border=1 >
<thead>
<tr>
  <td>Host</td>
  <td>Total keys</td>
  <td>Version</td>
  <td>IP:Port</td>
  <td>Last check (UTC)</td>
</tr>
</thead>
<tbody>'
}

function print_header () {
echo '<!doctype html public "-//W3C//DTD HTML 4.0 Transitional//EN">
<html>
<head>
<title>Keyserver statistics for gnupg.net</title>
<meta http-equiv="Content-Type" content="text/html;charset=utf-8" >
</head>
<body>
<h1>Keyserver statistics for gnupg.net</h1>
<p>
This table shows statistics for the keyservers in the
keys.gnupg.net and http-keys.gnupg.net pools.
</p>

'
}


function print_footer2 () {
echo '</tbody>
</table>'
}


function print_footer () {
print_footer2

echo '<br><br><br><br><br><hr>
<div align="left"><font size="-1">
Notifications on status changes are automatically posted to the
<a href="http://lists.gnupg.org/mailman/listinfo/keystats-gnupg-net"
>keystats-gnupg-net</a> mailing list.  You are welcome to subscribe
to this announce-only list.
</font></div>
</body>
</html>'
}


function make_row () {
    awk -v scheme=$1 '
$1 == "Server" { server = $2; port = $3}
$1 == "Host" { host = $2 }
$1 == "Total" { nkeys = $2 }
$1 == "Version" { version = $2 }
$1 == "Timestamp" { timestamp = $2 " " $3 }

END { if (host) {
        href = "<a href=\"http://" host ":" port "/pks/lookup?op=stats\">"
        tmp = "<tr><td>" href scheme "://" host "</a></td>" \
              "<td align=\"right\">" nkeys \
              "</td><td>" version "</td>" ;
      } else {
        tmp = "<tr><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td>" ;
      }
      print tmp "<td>" server ":" port "</td><td>" timestamp "</td></tr>" 
} 
'
}




host=""
failed_hosts=no
if [ "$1" == "--failed" ]; then
    failed_hosts=yes
    failed_hosts_file_orig="$2"
    if [ -z "$failed_hosts_file_orig" ]; then
        echo "usage: sks-stats.sh --failed FILENAME" >&2
        exit 1
    fi
    shift
    shift
    failed_hosts_file="${failed_hosts_file_orig}.tmp"
    : >> "${failed_hosts_file_orig}"
    : > "${failed_hosts_file}"
else
    failed_hosts_file="/dev/null"
fi
if [ "$1" == "--hosts" ]; then
    hostsfile="$2"
    shift
    shift
else
    hostsfile=""
fi
[ -n "$1" ] && host="$1"


if [ -z "$host" ]; then
   print_header 
   print_header2 "<h2>keys.gnupg.net</h2>"
   (
       host keys.gnupg.net | awk '/has address/ { print $4 }' \
           | while read host; do
           get_stats $host 11371 | make_row "hkp" 
       done
   ) | sort
   print_footer2
   print_header2 "<h2>http-keys.gnupg.net</h2>"
   (
       host http-keys.gnupg.net | awk '/has address/ { print $4 }' \
           | while read host; do
           get_stats $host 80 | make_row "http" 
       done
   ) | sort
else
  print_header
  get_stats $host 11371 | make_row "hkp"
fi
print_footer
if [ $failed_hosts = "yes" ]; then
    if cmp "${failed_hosts_file_orig}" "${failed_hosts_file}" >/dev/null ; then
        rm "${failed_hosts_file}"
    else
        echo "changes in failed hosts detected" >&2
        mv "${failed_hosts_file}" "${failed_hosts_file_orig}"
        : > "${failed_hosts_file_orig}.now"
    fi
fi
