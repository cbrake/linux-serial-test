#!/bin/bash

# Run linux-serial-test on the specified serial port at all standard baudrates.
# If no port is specified, probe. If one exists, use it. Otherwise give a warning.

trap cleanup INT
cleanup() {
    exit
}

main() {
    if [[ $# -eq 0 ]]; then
	port=($(chooseport))
	if [[ ${#port[@]} -eq 1 ]]; then
	    set -- $port
	elif [[ ${#port[@]} -eq 0 ]]; then
	    echo "Error: No working serial ports found." >&2
	    exit 1
	else
	    echo "${#port[@]} serial ports found. Please specify one of ${port[@]}." >&2
	    exit 1
	fi
    fi

    if [[ $# -eq 0 ]]; then
	echo "Usage: testallspeeds.sh <serialdevice> [speeds ...]"
	exit
    fi

    device="$1"; shift

    if [[ $# -eq 0 ]]; then
	set -- 1200 2400 4800 9600 19200 38400 57600 115200 230400 460800 500000 576000 921600 1000000 1152000 1500000 2000000 2500000 3000000 3500000 4000000
    fi

    declare -A results
    for baud; do
#	old=$(stty -F $device speed $baud)
	./linux-serial-test -p $device -f -n -o 2 -i 2 -b $baud
	results+=([$baud]=$?)
	echo
    done

    for b in ${!results[@]}; do
	status=${results[$b]}
	if [[ $status -eq 0 ]]; then
	    echo "$b	OK"
	else
	    echo "$b	NOGO"
	fi
    done | sort -n 
}


chooseport() {
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





main "$@"
