#!/bin/sh
#

# Example driver:
# #!/bin/sh
# 
# outfile="/var/www/217.69.76.60/keystats.gnupg.net/htdocs/index.html"
# failedfile="/var/www/217.69.76.60/keystats.gnupg.net/htdocs/failed-hosts"
# 
# rm "$failedfile.now" 2>/dev/null || true
# $HOME/bin/sks-stats.sh --failed-hosts "$failedfile" >"$outfile".tmp 2>/dev/null
# mv "$outfile".tmp "$outfile"
# if [ -f "$failedfile.now" ]; then
#    rm "$failedfile.now" 2>/dev/null
#    mail -s "keys.gnupg.net status change detected" wk@gnupg.org < "$failedfile"
# fi
# 


function get_stats () {
   local host
   local port

   host="$1"    
   port="$2"

  echo "retrieving $host:$port" >&2

  echo "Server $host $port"
  ( wget -O - -T 30 -t 2 "http://$host:$port/pks/lookup?op=stats" || echo ) | \
      awk -v failed="${failed_hosts_file}" -v hostip="$host" '
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
    print hostip >> failed
  }
}
'
echo "Timestamp $(date -u '+%F %T')"
}


function print_header () {
echo '<!doctype html public "-//W3C//DTD HTML 4.0 Transitional//EN">
<html>
<head><title>Keyserver statistics for keys.gnupg.net</title></head>
<body>
<h1>Keyserver statistics for keys.gnupg.net</h1>
<p>
This table shows statistics for the keyservers in the
keys.gnupg.net and http-keys.gnupg.net pools.
</p>

<table cellpadding=5 cellspacing=1 border=1 >
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


function print_footer () {
echo '</tbody>
</table>
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
if [ "$1" == "--failed-hosts" ]; then
    failed_hosts=yes
    failed_hosts_file_orig="$2"
    if [ -z "$failed_hosts_file_orig" ]; then
        echo "usage: sks-stats.sh --failed-hosts FILENAME" >&2
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
[ -n "$1" ] && host="$1"


if [ -z "$host" ]; then
   print_header 
   (
       host keys.gnupg.net | awk '/has address/ { print $4 }' \
           | while read host; do
           get_stats $host 11371 | make_row "hkp" 
       done
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
