linux-serial-test
=================

# Linux Serial Test Application

# Usage

    Usage: linux-serial-test [OPTION]

      -h, --help
      -b, --baud        Baud rate, 115200, etc (115200 is default)
      -p, --port        Port (/dev/ttyS0, etc) (must be specified)
      -d, --divisor     UART Baud rate divisor (can be used to set custom baud rates)
      -R, --rx_dump     Dump Rx data (ascii, raw)
      -T, --detailed_tx Detailed Tx data
      -s, --stats       Dump serial port stats every 5s
      -S, --stop-on-err Stop program if we encounter an error
      -y, --single-byte Send specified byte to the serial port
      -z, --second-byte Send another specified byte to the serial port
      -c, --rts-cts     Enable RTS/CTS flow control
      -e, --dump-err    Display errors
      -r, --no-rx       Don't receive data (can be used to test flow control)
                        when serial driver buffer is full
      -t, --no-tx       Don't transmit data
      -l, --rx-delay    Delay between reading data (ms) (can be used to test flow control)
      -q, --rs485       Enable RS485 direction control on port, and set delay
                        from when TX is finished and RS485 driver enable is
                        de-asserted. Delay is specified in bit times.

# Examples

## Stress test a connection

    linux-serial-test -s -e -p /dev/ttyO0 -b 3000000

This will send full bandwidth data with a counting pattern on the TX signal.
On any data received on RX, the program will look for a counting pattern and 
report any missing data in the pattern. This test can be done using a loopback
cable.

## Test flow control

    linux-serial-test -s -e -p /dev/ttyO0 -c -l 250

This enables RTS/CTS flow control and sends a counting pattern on the TX signal.
Reads are delayed by 250ms between reads, which will cause the buffer to fill up
and start using flow control. As before any missing data in the pattern is
reported, and if flow control is working correctly there should be none.

This test can be done using a loopback cable, or by running the program on both
ends of the connection.

## Output a pattern where you can easily verify baud rate with scope:

    linux-serial-test -y 0x55 -z 0x0 -p /dev/ttyO0 -b 3000000

This outputs 10 bits that are easy to measure, and then multiply by 10
in your head to get baud rate.

See the measure-baud-rate-example.png file in this project for an example.
