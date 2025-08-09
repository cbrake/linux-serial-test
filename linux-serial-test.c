// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <poll.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <linux/serial.h>
#include <errno.h>
#include <sys/file.h>
#include <signal.h>
#include <stdbool.h>
#include <locale.h>	    /* For numeric grouping commas to mark thousands */
#include <assert.h>	    /* For sanity checking */
#include <asm-generic/termbits-common.h> /* For speed_t (BOTHER baudrate)  */
static const speed_t _speed_t_max = -1;	 /* typically unsigned int */

#include "setbaudrate.h"	/* For set_custom_baud() via termios2 */

/*
 * glibc for MIPS has its own bits/termios.h which does not define
 * CMSPAR, so we vampirise the value from the generic bits/termios.h
 */
#ifndef CMSPAR
#define CMSPAR 010000000000
#endif

/*
 * Define modem line bits
 */
#ifndef TIOCM_LOOP
#define TIOCM_LOOP	0x8000
#endif

// command line args
int _cl_baud = 0;
char *_cl_port = NULL;
int _cl_divisor = 0;
int _cl_rx_dump = 0;
int _cl_rx_dump_ascii = 0;
int _cl_tx_detailed = 0;
int _cl_rx_detailed = 0;
int _cl_stats = 0;
int _cl_stop_on_error = 0;
int _cl_single_byte = -1;
int _cl_another_byte = -1;
int _cl_rts_cts = 0;
int _cl_2_stop_bit = 0;
int _cl_parity = 0;
int _cl_odd_parity = 0;
int _cl_stick_parity = 0;
int _cl_loopback = 0;
int _cl_dump_err = 0;
int _cl_no_rx = 0;
int _cl_no_tx = 0;
int _cl_rx_delay = 0;
int _cl_tx_delay = 0;
int _cl_tx_bytes = 0;
int _cl_rs485_after_delay = -1;
int _cl_rs485_before_delay = 0;
int _cl_rs485_rts_after_send = 0;
int _cl_do_not_touch_modem_lines = 0;
int _cl_tx_time = 0;
int _cl_rx_time = 0;
int _cl_tx_wait = 0;
int _cl_ascii_range = 0;
int _cl_write_after_read = 0;
int _cl_rx_timeout_ms = 2000;
int _cl_tx_timeout_ms = 2000;
int _cl_error_on_timeout = 0;
int _cl_no_icount = 0;
int _cl_flush_buffers = 0;

// Module variables
unsigned char _write_count_value = 0;
unsigned char _read_count_value = 0;
int _fd = -1;
unsigned char * _write_data;
ssize_t _write_size;
int _e_baud = 0;
int _ss_baud_base = 0;
int _ss_custom_divisor = 0;
bool _is_standard_baud = 0;

// keep our own counts for cases where the driver stats don't work
long long int _write_count = 0;
long long int _read_count = 0;
long long int _error_count = 0;

volatile sig_atomic_t sigint_received = 0;
void sigint_handler(int s)
{
	sigint_received += 1;

	//if it hangs in the loop or afterwards, this one gives the opportunity to stop right away
	if (sigint_received > 3) {
		exit(-1);
	}
}

static int bitsperframe() {
	/* bits per frame = data bits, + start bit + stop bit + parity bit if used. */
	int data_bits = 8;
	int start_bit = 1;
	int stop_bits = 1 + _cl_2_stop_bit;
	int parity_bit = _cl_parity;

	return data_bits + start_bit + stop_bits + parity_bit;
}

static int disable_closing_wait()
{
	// Disable Linux's 30 second wait for close() at slow baudrates.
	// (For no apparent reason, Linux requires root permissions for this.)
	unsigned short oldcw = 3000;
	struct serial_struct ss;
	int eta = 999999999;

	// Don't bother trying if it won't take long to drain.
	int baud = _e_baud?_e_baud:_cl_baud;
	if (baud) {
		eta = ( _write_count - _read_count) * bitsperframe() / baud;
	}
	if (eta <= 2) {
		return -1;
	}

	if (ioctl(_fd, TIOCGSERIAL, &ss) < 0) {
		// return silently as some devices do not support TIOCGSERIAL
		return -1;
	}

	oldcw = ss.closing_wait;
	if (oldcw == ASYNC_CLOSING_WAIT_NONE) {
		return -1;
	}

	ss.closing_wait = ASYNC_CLOSING_WAIT_NONE;
	if (ioctl(_fd, TIOCSSERIAL, &ss) < 0) {
		perror("TIOCSSERIAL ASYNC_CLOSING_WAIT_NONE");
		fprintf(stderr, "Estimated time to drain: %d seconds", eta);
		if (eta > oldcw/100) {
			fprintf(stderr, " (closing_wait max is %ds)", oldcw/100);
		}
		fprintf(stderr, "\n");
		return -1;
	}

	return oldcw;
}

static void reenable_closing_wait(unsigned short oldcw)
{
	// Re-open the serial port temporarily and reset closing_wait
	struct serial_struct ss;

	int fd = open(_cl_port, O_RDWR | O_NONBLOCK);
	if (ioctl(fd, TIOCGSERIAL, &ss) >= 0) {
		ss.closing_wait = oldcw;
		if (ioctl(fd, TIOCSSERIAL, &ss) < 0) {
			int ret = -errno;
			perror("TIOCSSERIAL reenable closing wait");
			exit(ret);
		}
	}
	close(fd);
}

static void close_no_waiting(int fd) {
	int oldcw = disable_closing_wait();
	flock(_fd, LOCK_UN);
	close(fd);
	if (oldcw >= 0) {
		reenable_closing_wait(oldcw);
	}
}

static void exit_handler(void)
{
	printf("Exit handler: Cleaning up ...\n");

	if (_fd >= 0) {
		tcflush(_fd, TCIOFLUSH);
		close_no_waiting(_fd);
	}

	if (_cl_port) {
		free(_cl_port);
		_cl_port = NULL;
	}

	if (_write_data) {
		free(_write_data);
		_write_data = NULL;
	}
}

static void dump_data(unsigned char * b, int count)
{
	printf("%i bytes: ", count);
	int i;
	for (i=0; i < count; i++) {
		printf("%02x ", b[i]);
	}

	printf("\n");
}

static void dump_data_ascii(unsigned char * b, int count)
{
	int i;
	for (i=0; i < count; i++) {
		printf("%c", b[i]);
	}
}

/* setserial's TIOCSSERIAL with custom_divisor is deprecated. Use termios2 instead. */
static void set_baud_divisor(int speed, int custom_divisor)
{
	// default baud was not found, so try to set a custom divisor
	struct serial_struct ss;
	int ret;
 
	/* Note: this change affects the *next* open() of the serial port! */
	/* This is just a temporary open() */
	int fd=open(_cl_port, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		ret = -errno;
		perror("Error opening serial port in set_baud_divisor");
		exit(ret);
	}

	if (ioctl(fd, TIOCGSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCGSERIAL in set_baud_divisor failed");
		exit(ret);
	}

	if (ss.baud_base == 0) {
		fprintf(stderr, "Cannot set custom divisor as baud_base is zero\n");
		exit(-EINVAL);
	}		

	/* Note: SPD is deprecated, but we still want to be able to test it */
	ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
	if (custom_divisor) {
		ss.custom_divisor = custom_divisor;
	} else {
		ss.custom_divisor = (ss.baud_base + (speed/2)) / speed;
		int closest_speed = ss.baud_base / ss.custom_divisor;

		if (closest_speed < speed * 98 / 100 || closest_speed > speed * 102 / 100) {
			fprintf(stderr, "Cannot set speed to %d, closest is %d\n", speed, closest_speed);
			exit(-EINVAL);
		}

		printf("closest baud = %i, base = %i, divisor = %i\n", closest_speed, ss.baud_base,
				ss.custom_divisor);
	}

	if (ioctl(fd, TIOCSSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCSSERIAL failed");
		exit(ret);
	}

	close(fd);

	/* Stash the baudrate etc for printing later */
	_ss_baud_base = ss.baud_base;
	_ss_custom_divisor = ss.custom_divisor;
	_cl_baud = ss.baud_base / ss.custom_divisor;
//	printf("Custom divisor of %'i / %i = %'i baud\n",
//	       ss.baud_base, ss.custom_divisor, _cl_baud);
}

static void clear_custom_speed_flag()
{
	struct serial_struct ss;
	int ret;

	if (ioctl(_fd, TIOCGSERIAL, &ss) < 0) {
		// return silently as some devices do not support TIOCGSERIAL
		return;
	}

	if ((ss.flags & ASYNC_SPD_MASK) != ASYNC_SPD_CUST)
		return;

	ss.flags &= ~ASYNC_SPD_MASK;

	if (ioctl(_fd, TIOCSSERIAL, &ss) < 0) {
		ret = -errno;
		perror("TIOCSSERIAL failed");
		exit(ret);
	}
}

// converts integer baud to Linux define
static int get_baud(int baud)
{
	switch (baud) {
	case 1200:
		return B1200;
	case 2400:
		return B2400;
	case 4800:
		return B4800;
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
#ifdef B1000000
	case 1000000:
		return B1000000;
#endif
#ifdef B1152000
	case 1152000:
		return B1152000;
#endif
#ifdef B1500000
	case 1500000:
		return B1500000;
#endif
#ifdef B2000000
	case 2000000:
		return B2000000;
#endif
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return -1;
	}
}

void set_modem_lines(int fd, int bits, int mask)
{
	if (_cl_do_not_touch_modem_lines)
		return;

	int status, ret;

	if (ioctl(fd, TIOCMGET, &status) < 0) {
		ret = -errno;
		perror("TIOCMGET failed");
		exit(ret);
	}

	status = (status & ~mask) | (bits & mask);

	if (ioctl(fd, TIOCMSET, &status) < 0) {
		ret = -errno;
		perror("TIOCMSET failed");
		exit(ret);
	}
}

static void display_help(void)
{
	printf("Usage: linux-serial-test [OPTION]\n"
			"\n"
			"  -h, --help\n"
			"  -b, --baud               Baud rate, 115200, etc (115200 is default)\n"
			"  -p, --port               Port (/dev/ttyS0, etc) (must be specified)\n"
			"  -d, --divisor            UART Baud rate divisor (can be used to set custom baud rates)\n"
			"  -D, --rx_dump            Dump Rx data (ascii, raw)\n"
			"  -T, --detailed_tx        Detailed Tx data\n"
			"  -R, --detailed_rx        Detailed Rx data\n"
			"  -s, --stats              Dump serial port stats every 5s\n"
			"  -S, --stop-on-err        Stop program if we encounter an error\n"
			"  -y, --single-byte        Send specified byte to the serial port\n"
			"  -z, --second-byte        Send another specified byte to the serial port\n"
			"  -c, --rts-cts            Enable RTS/CTS flow control\n"
			"  -B, --2-stop-bit         Use two stop bits per character\n"
			"  -P, --parity             Use parity bit (odd, even, mark, space)\n"
			"  -k, --loopback           Use internal hardware loop back\n"
			"  -K, --write-follow       Write follows the read count (can be used for multi-serial loopback)\n"
			"  -e, --dump-err           Display errors\n"
			"  -r, --no-rx              Don't receive data (can be used to test flow control)\n"
			"                           when serial driver buffer is full\n"
			"  -t, --no-tx              Don't transmit data\n"
			"  -l, --rx-delay           Delay between reading data (ms) (can be used to test flow control)\n"
			"  -a, --tx-delay           Delay between writing data (ms)\n"
			"  -w, --tx-bytes           Number of bytes for each write (default is to repeatedly write 1024 bytes\n"
			"                           until no more are accepted)\n"
			"  -q, --rs485              Enable RS485 direction control on port, and set delay from when TX is\n"
			"                           finished and RS485 driver enable is de-asserted. Delay is specified in\n"
			"                           bit times. To optionally specify a delay from when the driver is enabled\n"
			"                           to start of TX use 'after_delay.before_delay' (-q 1.1)\n"
			"  -Q, --rs485_rts          Deassert RTS on send, assert after send. Omitting -Q inverts this logic.\n"
			"  -m, --no-modem           Do not clobber against any modem lines.\n"
			"  -o, --tx-time            Number of seconds to transmit for (defaults to 0, meaning no limit)\n"
			"  -i, --rx-time            Number of seconds to receive for (defaults to 0, meaning no limit)\n"
			"  -A, --ascii              Output bytes range from 32 to 126 (default is 0 to 255)\n"
			"  -I, --rx-timeout         Receive timeout\n"
			"  -O, --tx-timeout         Transmission timeout\n"
			"  -W, --tx-wait            Number of seconds to wait before to transmit (defaults to 0, meaning no wait)\n"
			"  -Z, --error-on-timeout   Treat timeouts as errors\n"
			"  -n, --no-icount          Do not request driver for counts of input serial line interrupts (TIOCGICOUNT)\n"
			"  -f, --flush-buffers      Flush RX and TX buffers before starting\n"
			"\n"
		);
}

static void process_options(int argc, char * argv[])
{
	for (;;) {
		int option_index = 0;
		static const char *short_options = "hb:p:d:D:TRsSy:z:cBertq:Qml:a:w:o:i:P:kKAI:O:W:Znf";
		static const struct option long_options[] = {
			{"help", no_argument, 0, 0},
			{"baud", required_argument, 0, 'b'},
			{"port", required_argument, 0, 'p'},
			{"divisor", required_argument, 0, 'd'},
			{"rx_dump", required_argument, 0, 'D'},
			{"detailed_tx", no_argument, 0, 'T'},
			{"detailed_rx", no_argument, 0, 'R'},
			{"stats", no_argument, 0, 's'},
			{"stop-on-err", no_argument, 0, 'S'},
			{"single-byte", required_argument, 0, 'y'},
			{"second-byte", required_argument, 0, 'z'},
			{"rts-cts", no_argument, 0, 'c'},
			{"2-stop-bit", no_argument, 0, 'B'},
			{"parity", required_argument, 0, 'P'},
			{"loopback", no_argument, 0, 'k'},
			{"write-follows", no_argument, 0, 'K'},
			{"dump-err", no_argument, 0, 'e'},
			{"no-rx", no_argument, 0, 'r'},
			{"no-tx", no_argument, 0, 't'},
			{"rx-delay", required_argument, 0, 'l'},
			{"tx-delay", required_argument, 0, 'a'},
			{"tx-bytes", required_argument, 0, 'w'},
			{"rs485", required_argument, 0, 'q'},
			{"rs485_rts", no_argument, 0, 'Q'},
			{"no-modem", no_argument, 0, 'm'},
			{"tx-time", required_argument, 0, 'o'},
			{"rx-time", required_argument, 0, 'i'},
			{"tx-wait", required_argument, 0, 'W'},
			{"ascii", no_argument, 0, 'A'},
			{"rx-timeout", required_argument, 0, 'I'},
			{"tx-timeout", required_argument, 0, 'O'},
			{"error-on-timeout", no_argument, 0, 'Z'},
			{"no-icount", no_argument, 0, 'n'},
			{"flush-buffers", no_argument, 0, 'f'},
			{0,0,0,0},
		};

		int c = getopt_long(argc, argv, short_options,
				long_options, &option_index);

		if (c == EOF) {
			break;
		}

		switch (c) {
		case 0:
		case 'h':
			display_help();
			exit(0);
			break;
		case 'b':
			double f = atof(optarg);
			if (f > _speed_t_max || f < 0) {
				fprintf(stderr, "ERROR: Invalid baud rate %'.0f ", f);
				fprintf(stderr, "(termios2 max is %'llu)\n", (long long unsigned int) _speed_t_max);
				exit(-EINVAL);
			}
			_cl_baud = (int)atof(optarg); 	/* Allow 3E6 instead of 3000000 */
			break;
		case 'p':
			_cl_port = strdup(optarg);
			break;
		case 'd':
			_cl_divisor = strtol(optarg, NULL, 0);
			break;
		case 'D':
			_cl_rx_dump = 1;
			_cl_rx_dump_ascii = !strcmp(optarg, "ascii");
			break;
		case 'T':
			_cl_tx_detailed = 1;
			break;
		case 'R':
			_cl_rx_detailed = 1;
			break;
		case 's':
			_cl_stats = 1;
			break;
		case 'S':
			_cl_stop_on_error = 1;
			break;
		case 'y': {
			char * endptr;
			_cl_single_byte = strtol(optarg, &endptr, 0);
			break;
		}
		case 'z': {
			char * endptr;
			_cl_another_byte = strtol(optarg, &endptr, 0);
			break;
		}
		case 'c':
			_cl_rts_cts = 1;
			break;
		case 'B':
			_cl_2_stop_bit = 1;
			break;
		case 'P':
			_cl_parity = 1;
			_cl_odd_parity = (!strcmp(optarg, "mark")||!strcmp(optarg, "odd"));
			_cl_stick_parity = (!strcmp(optarg, "mark")||!strcmp(optarg, "space"));
			break;
		case 'k':
			_cl_loopback = 1;
			break;
		case 'K':
			_cl_write_after_read = 1;
			break;
		case 'e':
			_cl_dump_err = 1;
			break;
		case 'r':
			_cl_no_rx = 1;
			break;
		case 't':
			_cl_no_tx = 1;
			break;
		case 'l': {
			char *endptr;
			_cl_rx_delay = strtol(optarg, &endptr, 0);
			break;
		}
		case 'a': {
			char *endptr;
			_cl_tx_delay = strtol(optarg, &endptr, 0);
			break;
		}
		case 'w': {
			char *endptr;
			_cl_tx_bytes = strtol(optarg, &endptr, 0);
			break;
		}
		case 'q': {
			char *endptr;
			_cl_rs485_after_delay = strtol(optarg, &endptr, 0);
			_cl_rs485_before_delay = strtol(endptr+1, &endptr, 0);
			break;
		}
		case 'Q':
			_cl_rs485_rts_after_send = 1;
			break;
		case 'm':
			_cl_do_not_touch_modem_lines = 1;
			break;
		case 'o': {
			char *endptr;
			_cl_tx_time = strtol(optarg, &endptr, 0);
			break;
		}
		case 'i': {
			char *endptr;
			_cl_rx_time = strtol(optarg, &endptr, 0);
			break;
		}
		case 'W': {
			char *endptr;
			_cl_tx_wait = strtol(optarg, &endptr, 0);
			break;
		}
		case 'A':
			_cl_ascii_range = 1;
			break;
		case 'I':
			_cl_rx_timeout_ms = atoi(optarg);
			break;
		case 'O':
			_cl_tx_timeout_ms = atoi(optarg);
			break;
		case 'Z':
			_cl_error_on_timeout = 1;
			break;
		case 'n':
			_cl_no_icount = 1;
			break;
		case 'f':
			_cl_flush_buffers = 1;
			break;
		}
	}
}

static void print_requested_baudrate() {
	if (_cl_baud == 0) return;
	printf("REQUESTED BAUDRATE: ");
	printf(_is_standard_baud?"B":" ");
	printf("%'12d", _cl_baud);
	if (_error_count)
		printf("\t!UNRELIABLE!");
	printf("\n");
	if (_ss_custom_divisor) {
		printf("\t\t   = %'12d / %d custom divisor\n",
		       _ss_baud_base, _ss_custom_divisor);
	}
}	

double _errpercent = 0;
static void print_estimated_baudrate(double duration) {
	int rx = _read_count;
	int bits = bitsperframe();
	double estimated = rx * bits / duration;
        _errpercent =  ( 100.0*(_cl_baud - estimated) / _cl_baud );
	if (_errpercent<0) _errpercent=-_errpercent;
	printf("ESTIMATED BAUDRATE: %'16.2f", rx * bits / duration);
	if ( (_errpercent >= 1.0) && (_cl_baud > 0) )
		printf("\t!+/- %.2f%% !", _errpercent);
	printf("\n");
	printf("\t(%d frames, %d bits each, received in %.2f seconds)\n",
	       rx, bits, duration);
}	

static double estimated_baudrate(double duration) {
	int rx = _read_count;
	int bits = bitsperframe();
	return rx * bits / duration;
}	

static void dump_serial_port_stats(void)
{
	struct serial_icounter_struct icount = { 0 };

	printf("%s: count for this session: rx=%lld, tx=%lld, rx err=%lld\n", _cl_port, _read_count, _write_count, _error_count);

	if (!_cl_no_icount) {
		int ret = ioctl(_fd, TIOCGICOUNT, &icount);
		if (ret < 0) {
			perror("Error getting TIOCGICOUNT");
		} else {
			printf("%s: TIOCGICOUNT: ret=%i, rx=%i, tx=%i, frame = %i, overrun = %i, parity = %i, brk = %i, buf_overrun = %i\n",
					_cl_port, ret, icount.rx, icount.tx, icount.frame, icount.overrun, icount.parity, icount.brk,
					icount.buf_overrun);
		}
	}
}

static unsigned char next_count_value(unsigned char c)
{
	c++;
	if (_cl_ascii_range && c == 127)
		c = 32;
	return c;
}

static void process_read_data(void)
{
	const int RBSIZE = 1024;
	unsigned char rb[RBSIZE];
	int c = read(_fd, &rb, RBSIZE);
	if (c > 0) {
		if (_cl_rx_dump) {
			if (_cl_rx_dump_ascii)
				dump_data_ascii(rb, c);
			else
				dump_data(rb, c);
		}

		// verify read count is incrementing
		int i;
		for (i = 0; i < c; i++) {
			if (rb[i] != _read_count_value) {
				if (_cl_dump_err) {
					printf("Error, count: %lld, expected %02x, got %02x c %x\n",
					       _read_count + i, _read_count_value, rb[i], c);
				}
				_error_count++;
				if (_cl_stop_on_error) {
					dump_serial_port_stats();
					exit(-EIO);
				}
				_read_count_value = rb[i];
			}
			_read_count_value = next_count_value(_read_count_value);
		}
		_read_count += c;
	}
	if ( (c == -1) && (errno != EAGAIN) ) {
		perror("read failed");
	}
	
	if (_cl_rx_detailed) {
		printf("Read %lld bytes %s\n", _read_count,
		       (_read_count < RBSIZE)?"":"(buffer limit)");
	}
}

static void process_write_data(void)
{
	ssize_t count = 0;
	ssize_t actual_write_size = 0;
	int repeat = (_cl_tx_bytes == 0);

	do
	{
		if (_cl_write_after_read == 0) {
			actual_write_size = _write_size;
		} else {
			actual_write_size = _read_count > _write_count ? _read_count - _write_count : 0;
			if (actual_write_size > _write_size) {
				actual_write_size = _write_size;
			}
		}
		if (actual_write_size == 0) {
			break;
		}

		ssize_t i;
		for (i = 0; i < actual_write_size; i++) {
			_write_data[i] = _write_count_value;
			_write_count_value = next_count_value(_write_count_value);
		}

		ssize_t c = write(_fd, _write_data, actual_write_size);

		if (c < 0) {
			if (errno != EAGAIN) {
				printf("write failed - errno=%d (%s)\n", errno, strerror(errno));
			}
			c = 0;
		}

		count += c;

		if (c < actual_write_size) {
			_write_count_value = _write_data[c];
			repeat = 0;
		}
	} while (repeat);

	_write_count += count;

	if (_cl_tx_detailed)
		printf("wrote %zd bytes\n", count);
}


static void setup_serial_port(int baud)
{
	struct termios newtio;
	struct serial_rs485 rs485;
	int ret;

	assert (_fd < 0);

	_fd = open(_cl_port, O_RDWR | O_NONBLOCK);
	if (_fd < 0) {
		ret = -errno;
		perror("Error opening serial port");
		exit(ret);
	}

	/* Lock device file */
	if (flock(_fd, LOCK_EX | LOCK_NB) < 0) {
		ret = -errno;
		perror("Error failed to lock device file");
		exit(ret);
	}

	tcgetattr(_fd,&newtio);			/* get current port settings  */
	/* man termios get more info on below settings */
	newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;

	if (cfsetispeed (&newtio, baud)) {
		ret = -errno;
		perror("cfsetispeed");
		exit(ret);
	}
	if (cfsetospeed (&newtio, baud)) {
		ret = -errno;
		perror("cfsetospeed");
		exit(ret);
	}

	if (_cl_rts_cts) {
		newtio.c_cflag |= CRTSCTS;
	}

	if (_cl_2_stop_bit) {
		newtio.c_cflag |= CSTOPB;
	}

	if (_cl_parity) {
		newtio.c_cflag |= PARENB;
		if (_cl_odd_parity) {
			newtio.c_cflag |= PARODD;
		}
		if (_cl_stick_parity) {
			newtio.c_cflag |= CMSPAR;
		}
	}

	newtio.c_iflag = 0;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;

	// block for up till 128 characters
	newtio.c_cc[VMIN] = 128;

	// 0.5 seconds read timeout
	newtio.c_cc[VTIME] = 5;

	/* now clean the modem line and activate the settings for the port */
	tcflush(_fd, TCIOFLUSH);
	tcsetattr(_fd,TCSANOW,&newtio);

	/* enable/disable rs485 direction control, first check if RS485 is supported */
	if(ioctl(_fd, TIOCGRS485, &rs485) < 0) {
		if (_cl_rs485_after_delay >= 0) {
			/* error could be because hardware is missing rs485 support so only print when actually trying to activate it */
			perror("Error getting RS-485 mode");
		}
	} else {
		if (rs485.flags & SER_RS485_ENABLED) {
			printf("RS485 already enabled on port, ignoring delays if set\n");
		} else {
			if (_cl_rs485_after_delay >= 0) {
				/* enable RS485 */
				rs485.flags |= SER_RS485_ENABLED | SER_RS485_RX_DURING_TX |
					(_cl_rs485_rts_after_send ? SER_RS485_RTS_AFTER_SEND : SER_RS485_RTS_ON_SEND);
				rs485.flags &= ~(_cl_rs485_rts_after_send ? SER_RS485_RTS_ON_SEND : SER_RS485_RTS_AFTER_SEND);
				rs485.delay_rts_after_send = _cl_rs485_after_delay;
				rs485.delay_rts_before_send = _cl_rs485_before_delay;
				if(ioctl(_fd, TIOCSRS485, &rs485) < 0) {
					perror("Error setting RS-485 mode");
				}
			} else {
				/* disable RS485 */
				rs485.flags &= ~(SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND | SER_RS485_RTS_AFTER_SEND);
				rs485.delay_rts_after_send = 0;
				rs485.delay_rts_before_send = 0;
				if(ioctl(_fd, TIOCSRS485, &rs485) < 0) {
					perror("Error setting RS-232 mode");
				}
			}
		}
	}
}

static int diff_ms(const struct timespec *t1, const struct timespec *t2)
{
	struct timespec diff;

	diff.tv_sec = t1->tv_sec - t2->tv_sec;
	diff.tv_nsec = t1->tv_nsec - t2->tv_nsec;
	if (diff.tv_nsec < 0) {
		diff.tv_sec--;
		diff.tv_nsec += 1000000000;
	}
	return (diff.tv_sec * 1000 + diff.tv_nsec/1000000);
}

static int diff_s(const struct timespec *t1, const struct timespec *t2)
{
	return t1->tv_sec - t2->tv_sec;
}

static const int _max_error_rv = 125;
static int compute_error_count(void)
{
	long long int result;
	if (_cl_no_rx == 1 || _cl_no_tx == 1)
		result = _error_count;
	else
		result = llabs(_write_count - _read_count) + _error_count;

	return (result > _max_error_rv) ? _max_error_rv : (int)result;
}

int main(int argc, char * argv[])
{
	setlocale(LC_ALL, "");
	printf("Linux serial test app\n");

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);
	atexit(&exit_handler); //does not work for SIGINT/SIGTERM without the previous signal handlers

	process_options(argc, argv);

	int wait_time = _cl_tx_wait;

	if (!_cl_port) {
		fprintf(stderr, "ERROR: Port argument required\n");
		display_help();
		exit(-EINVAL);
	}

	int baud = B115200;

	if (_cl_baud && !_cl_divisor)
		baud = get_baud(_cl_baud);

	/* Three different ways of calling setup_serial_port? This could be cleaner. */
	if (_cl_divisor) {
		/* -d custom divisor flag was given. */
		set_baud_divisor(_cl_baud, _cl_divisor);
		baud = B38400;
		setup_serial_port(B38400);
	} else if (baud <= 0) {
		/* _cl_baud was specified and is not one of the predefined baudrates. */
		setup_serial_port(B0);
		if (set_custom_baud(_fd, _cl_baud)) {
			printf("NOTE: termios2 failed to set non-standard baudrate, approximating using divisor\n");
			set_baud_divisor(_cl_baud, _cl_divisor);
			baud = B38400;
			setup_serial_port(B38400);
		}
	} else {
		/* The typical situation, baudrate is something normal like 115200 */
		_is_standard_baud = 1; 			/* Stashaway for printing out later */
		setup_serial_port(baud);
		/*
		 * The flag ASYNC_SPD_CUST might have already been set, so
		 * clear it to avoid confusing the kernel uart dirver.
		 */
		clear_custom_speed_flag();
	}

	set_modem_lines(_fd, _cl_loopback ? TIOCM_LOOP : 0, TIOCM_LOOP);

	if (_cl_single_byte >= 0) {
		unsigned char data[2];
		int bytes = 1;
		int written;
		data[0] = (unsigned char)_cl_single_byte;
		if (_cl_another_byte >= 0) {
			data[1] = (unsigned char)_cl_another_byte;
			bytes++;
		}
		written = write(_fd, &data, bytes);
		if (written < 0) {
			int ret = -errno;
			perror("write()");
			exit(ret);
		} else if (written != bytes) {
			fprintf(stderr, "ERROR: write() returned %d, not %d\n", written, bytes);
			exit(-EIO);
		}
		return 0;
	}

	_write_size = (_cl_tx_bytes == 0) ? 1024 : _cl_tx_bytes;

	_write_data = malloc(_write_size);
	if (_write_data == NULL) {
		fprintf(stderr, "ERROR: Memory allocation failed\n");
		exit(-ENOMEM);
	}

	if (_cl_ascii_range) {
		_read_count_value = _write_count_value = 32;
	}

	struct pollfd serial_poll;
	serial_poll.fd = _fd;
	if (!_cl_no_rx) {
		serial_poll.events |= POLLIN;
	} else {
		serial_poll.events &= ~POLLIN;
	}

	if (!_cl_no_tx) {
		serial_poll.events |= POLLOUT;
	} else {
		serial_poll.events &= ~POLLOUT;
	}

	if (_cl_flush_buffers) {
		printf("Flush RX buffer.\n");
		// Wait 100ms delay to let data arrive before flushing the I/O
		// buffers. This is a unfortunately a known workaround.
		usleep(100000);
		tcflush(_fd, TCIOFLUSH);
	}

	struct timespec start_time, last_stat, last_timeout, last_read, last_write;

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	last_stat = start_time;
	last_timeout = start_time;
	last_read = start_time;
	last_write = start_time;

	if (_cl_tx_wait)
		serial_poll.events &= ~POLLOUT;

	while (!(_cl_no_rx && _cl_no_tx) && !sigint_received ) {
		struct timespec current;
		int retval = poll(&serial_poll, 1, 1000);

		clock_gettime(CLOCK_MONOTONIC, &current);

		if (_cl_tx_wait) {
			if (diff_s(&current, &start_time) >= _cl_tx_wait) {
				_cl_tx_wait = 0;
				_cl_no_tx = 0;
				serial_poll.events |= POLLOUT;
				printf("Start transmitting.\n");
			} else {
				if (!_cl_no_tx) {
					_cl_no_tx = 1;
					serial_poll.events &= ~POLLOUT;
				}
			}
		}

		if (retval == -1) {
			perror("poll()");
		} else if (retval) {
			if (serial_poll.revents & POLLIN) {
				if (_cl_rx_delay) {
					// only read if it has been rx-delay ms
					// since the last read
					if (diff_ms(&current, &last_read) > _cl_rx_delay) {
						process_read_data();
						last_read = current;
					}
				} else {
					process_read_data();
					last_read = current;
				}
			}

			if (serial_poll.revents & POLLOUT) {
				if (_cl_tx_delay) {
					// only write if it has been tx-delay ms
					// since the last write
					if (diff_ms(&current, &last_write) > _cl_tx_delay) {
						process_write_data();
						last_write = current;
					}
				} else {
					process_write_data();
					last_write = current;
				}
			}
		}

		// Has it been at least a second since we reported a timeout? Have we ever reported a timeout?
		if ((diff_ms(&current, &last_timeout) > 1000) || (diff_ms(&last_timeout, &start_time) == 0)) {
			int rx_timeout, tx_timeout;

			// Has it been over two seconds since we transmitted or received data?
			rx_timeout = (!_cl_no_rx && diff_ms(&current, &last_read) > _cl_rx_timeout_ms);
			tx_timeout = (!_cl_no_tx && diff_ms(&current, &last_write) > _cl_tx_timeout_ms);
			// Special case - we don't want to warn about receive
			// timeouts at the end of a loopback test (where we are
			// no longer transmitting and the receive count equals
			// the transmit count).
			if (_cl_no_tx && _write_count != 0 && _write_count == _read_count) {
				rx_timeout = 0;
			}

			if (rx_timeout || tx_timeout) {
				const char *s;
				if (rx_timeout) {
					printf("%s: No data received for %.1fs.",
						   _cl_port, (double)diff_ms(&current, &last_read) / 1000);
					s = " ";
					if (_cl_error_on_timeout) {
						printf(" Exiting due to timeout.\n");
						exit(-ETIMEDOUT);
					}
				} else {
					s = "";
				}
				if (tx_timeout) {
					printf("%sNo data transmitted for %.1fs.",
						   s, (double)diff_ms(&current, &last_write) / 1000);
					if (_cl_error_on_timeout) {
						printf(" Exiting due to timeout.\n");
						exit(-ETIMEDOUT);
				}
				}
				printf("\n");
				last_timeout = current;
			}
		}

		if (_cl_stats) {
			if (current.tv_sec - last_stat.tv_sec > 5) {
				dump_serial_port_stats();
				last_stat = current;
			}
		}

		if (_cl_tx_time && !_cl_tx_wait) {
			if (current.tv_sec - start_time.tv_sec >= wait_time &&
				current.tv_sec - start_time.tv_sec - wait_time >= _cl_tx_time ) {
				_cl_tx_time = 0;
				_cl_no_tx = 1;
				serial_poll.events &= ~POLLOUT;
				printf("Stopped transmitting.\n");
			}
		}

		if (_cl_rx_time) {
			if (current.tv_sec - start_time.tv_sec >= _cl_rx_time) {
				_cl_rx_time = 0;
				_cl_no_rx = 1;
				serial_poll.events &= ~POLLIN;
				printf("Stopped receiving.\n");
			}
		}
	}

	printf("Terminating ...\n");
	tcflush(_fd, TCIOFLUSH);
	dump_serial_port_stats();
	print_requested_baudrate();
	print_estimated_baudrate(diff_ms(&last_read, &start_time)/1000.0);
	_e_baud = estimated_baudrate(diff_ms(&last_read, &start_time)/1000.0);
	set_modem_lines(_fd, 0, TIOCM_LOOP); //seems not to be relevant for RTS reset

	int rv = compute_error_count();
	if (rv == 0 && _cl_baud != 0 ) {
		if (_errpercent>1.0) rv = _max_error_rv + 1; 
	}
	return rv;
}
