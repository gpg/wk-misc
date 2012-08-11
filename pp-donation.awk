# pp-donation.awk                                           -*- awk -*-
#
# Extract donation details from a Paypal notification mail.  This is
# called from a shell script because we need to use gawk's -b option.

BEGIN {
    FS = ":"
}

FNR==1 && NR > 1 { result() }

/^Donation Details/ { in_details = 1; next }

!in_details && /^[Dd][Aa][Tt][Ee]:/ {
    date=trim($2 ":" $3 ":" $4)
    if (match(date, /^[a-zA-Z0-9,-: \t]+$/))
    {
        cmd = "date -d '" date "' +'%F'"
        cmd | getline date
        close(cmd)
    }
    else
    {
        date = "INVALID DATE"
    }
}

!in_details && /^This email confirms.*from/ {
    email = gensub(/.*\(([^)]+)\)\..*/, "\\1", 1)
}

!in_details  { next }

/^[ \t]*Total amount:/  {split($2, a, " "); sub(/,/, ".", a[1]); amount = a[1]}

/^The following.*Publish my sponsor name\?/ {
    publish = substr(trim($3), 1, 1)
}

/^Contributor:/  { name = trim($2) }

END {
    result()
}

function result() {
    printf "| %s | %s | %s | %s | %s |\n", date, amount, publish, name, email
    date=""
    amount=""
    publish=""
    name=""
    email=""
    in_details=0
}

function trim(s) {
    sub (/[ \t]+$/, "", s)
    sub (/^[ \t]+/, "", s)
    return s
}
