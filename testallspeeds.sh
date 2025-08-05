#!/bin/bash

trap cleanup INT
cleanup() {
    exit
}

if [[ $# -eq 0 ]]; then
    echo "Usage: testallspeeds.sh <serialdevice> [speeds ...]"
    echo "Possible serial devices: "
    setserial -g /dev/tty* 2>/dev/null | cut -f1 -d, | column
    exit
fi

device="$1"; shift

if [[ $# -eq 0 ]]; then
    set -- 1200 2400 4800 9600 19200 38400 57600 115200 230400 460800 500000 576000 921600 1000000 1152000 1500000 2000000 2500000 3000000 3500000 4000000
fi

for baud; do
    old=$(stty -F $device speed $baud)
    ./linux-serial-test -p $device -f -n -o 2 -i 2 -b $baud
    echo
done

