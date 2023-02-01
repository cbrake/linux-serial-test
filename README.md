# linux-serial-test

# Linux Serial Test Application

# Compiling

This is a very simple program -- there are several ways to compile it on Linux:

## directly using GCC

`gcc -o linux-serial-test linux-serial-test.c`

## Using CMake

- `cmake ./`
- `make`

# Usage

```
Usage: linux-serial-test [OPTION]

  -h, --help
  -b, --baud               Baud rate, 115200, etc (115200 is default)
  -p, --port               Port (/dev/ttyS0, etc) (must be specified)
  -d, --divisor            UART Baud rate divisor (can be used to set custom baud rates)
  -R, --rx_dump            Dump Rx data (ascii, raw)
  -T, --detailed_tx        Detailed Tx data
  -s, --stats              Dump serial port stats every 5s
  -S, --stop-on-err        Stop program if we encounter an error
  -y, --single-byte        Send specified byte to the serial port
  -z, --second-byte        Send another specified byte to the serial port
  -c, --rts-cts            Enable RTS/CTS flow control
  -B, --2-stop-bit         Use two stop bits per character
  -P, --parity             Use parity bit (odd, even, mark, space)
  -k, --loopback           Use internal hardware loop back
  -K, --write-follow       Write follows the read count (can be used for multi-serial loopback)
  -e, --dump-err           Display errors
  -r, --no-rx              Don't receive data (can be used to test flow control)
                           when serial driver buffer is full
  -t, --no-tx              Don't transmit data
  -l, --rx-delay           Delay between reading data (ms) (can be used to test flow control)
  -a, --tx-delay           Delay between writing data (ms)
  -w, --tx-bytes           Number of bytes for each write (default is to repeatedly write 1024 bytes
                           until no more are accepted)
  -q, --rs485              Enable RS485 direction control on port, and set delay from when TX is
                           finished and RS485 driver enable is de-asserted. Delay is specified in
                           bit times. To optionally specify a delay from when the driver is enabled
                           to start of TX use 'after_delay.before_delay' (-q 1.1)
  -Q, --rs485_rts          Deassert RTS on send, assert after send. Omitting -Q inverts this logic.
  -o, --tx-time            Number of seconds to transmit for (defaults to 0, meaning no limit)
  -i, --rx-time            Number of seconds to receive for (defaults to 0, meaning no limit)
  -A, --ascii              Output bytes range from 32 to 126 (default is 0 to 255)
  -I, --rx-timeout         Receive timeout
  -O, --tx-timeout         Transmission timeout
  -W, --tx-wait            Number of seconds to wait before to transmit (defaults to 0, meaning no wait)
  -Z, --error-on-timeout   Treat timeouts as errors
  -n, --no-icount          Do not request driver for counts of input serial line interrupts (TIOCGICOUNT)
  -f, --flush-buffers      Flush RX and TX buffers before starting
```


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
ends of the connection. For a two-port solution invoke the following command on
the receiver side:

    linux-serial-test -s -e -p /dev/ttyO1 -t -c -l 250

and on the transmitter side:

    linux-serial-test -s -e -p /dev/ttyO0 -r -c

## Stress test that can be used in a script

    linux-serial-test -s -e -p /dev/ttyO0 -b 115200 -o 5 -i 7

This transmits for five seconds and receives for seven seconds, after which it
will exit. The exit code will be zero if the number of received bytes matched
the number of transmitted bytes and the received pattern was correct, so this
can be used as part of an automated test script.

## Output a pattern where you can easily verify baud rate with scope:

    linux-serial-test -y 0x55 -z 0x0 -p /dev/ttyO0 -b 3000000

This outputs 10 bits that are easy to measure, and then multiply by 10
in your head to get baud rate.

![verify baud rate](https://github.com/cbrake/linux-serial-test/blob/master/measure-baud-rate-example.png)
