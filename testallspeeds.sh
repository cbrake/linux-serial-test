#!/bin/bash

# Run linux-serial-test on the specified serial port at all standard baudrates.
# If no port is specified, probe. If one exists, use it. Otherwise give a warning.

trap cleanup INT
cleanup() {
    exit
}

main() {
    getport "$@"

    if [[ $# -eq 0 ]]; then
	echo "Usage: testallspeeds.sh <serialdevice> [speeds ...]"
	exit
    fi

    device="$1"; shift

    if [[ $# -eq 0 ]]; then
	set -- 1200 2400 4800 9600 19200 38400 57600 115200 \
	    230400 460800 500000 576000 921600 1000000 1152000 \
	    1500000 2000000 2500000 3000000 3500000 4000000
    fi

    declare -A results
    for baud; do
	./linux-serial-test -p $device -f -n -o 2 -i 2 -b $baud
	results+=([$baud]=$?)
	echo
    done

    echo
    echo "SUMMARY"
    for b in ${!results[@]}; do
	status=${results[$b]}
	if [[ $status -eq 0 ]]; then
	    echo "$b	OK"
	else
	    echo "$b	failed ${strerr[${results[$b]}]}"
	fi
    done | sort -n 
}


availableports() {
    exec 5<&1 >/dev/tty
    echo "Possible serial devices: "
    for p in $(setserial -g /dev/tty* 2>/dev/null | cut -f1 -d,); do
	if [[ ! -r $p || ! -w $p ]]; then
	    echo "	$p: ERROR: Permission denied"
	    continue
	fi
	if ! stty -F $p >/dev/null 2>&1; then
	    echo "	$p: ERROR: Invalid port"
	    continue
	fi
	echo "	$p: Available"
	echo $p >&5
    done
}    

getport() {
    local port
    if [[ $1 =~ ^/ ]]; then port="$1"; shift;  fi
    if [[ $1 =~ ^[A-Za-z] ]]; then port="/dev/$1"; shift; fi
    if [[ $# -eq 0 ]]; then
	port=($(availableports))
	if [[ ${#port[@]} -eq 1 ]]; then
	    echo "Detected serial port at ${port[0]}"
	    set -- $port "$@"
	elif [[ ${#port[@]} -eq 0 ]]; then
	    echo "Error: No working serial ports found." >&2
	    exit 1
	else
	    echo "${#port[@]} serial ports found. Please specify one of ${port[@]}."
	    select port in "${port[@]}"; do
		if [[ $port ]]; then
		    set -- $port "$@"
		    break
		fi
	    done
	fi
    fi

    if [[ ! -e $port ]]; then
	echo "Error: Serial port $port does not exist." >&2
	exit 1
    fi	
    if [[ ! -r $port || ! -w $port ]]; then
	echo "Error: $port Permission denied." >&2
	exit 1
    fi	
    if ! stty -F $port >/dev/null 2>&1; then
	echo "Error: $port I/O error." >&2
	exit 1
    fi	
    if ! stty -F $port speed 9600 >/dev/null 2>&1; then
	echo "Error: $port I/O error." >&2
	exit 1
    fi	
}

init() {
    declare -ag strerr
    for ((i=255; i>=128; i--)); do strerr[$i]="Errno #$((256-i))"; done
    while read def name num desc; do
	if [[ "$def" != "#define" || -z "$desc" ]]; then continue; fi
	strerr[$((256-$num))]="$name: $desc"
    done < <(sed -r 's#/\*|\*/##g' /usr/include/asm-generic/errno{-base,}.h)

    strerr[0]="No errors"
    strerr[1]="1 transmission error"
    for ((i=2; i<125; i++)); do strerr[$i]="$i transmission errors"; done
    strerr[125]="Uncountably many transmission errors"
    strerr[126]="Baudrate error"
    strerr[127]="No such error"
}


init
main "$@"
