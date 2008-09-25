#!/bin/sh
#


function get_stats () {
   local host
   local port

   host="$1"    
   port="$2"

  echo "retrieving $host:$port" >&2

  echo "Server $host $port"
  wget -qO - -T 30 "http://$host:$port/pks/lookup?op=stats" | awk '
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
keys.gnupg.org and http-keys.gnupg.org pools.
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
 
