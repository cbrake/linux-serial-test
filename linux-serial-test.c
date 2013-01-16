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

// command line args
int _cl_baud = 0;
char _cl_port[50] = "";
int _cl_divisor = 0;
int _cl_rx_dump = 0;
int _cl_tx_detailed = 0;
int _cl_stats = 0;
int _cl_stop_on_error = 0;
int _cl_single_byte = -1;
int _cl_another_byte = -1;
int _cl_rts_cts = 0;
int _cl_dump_err = 0;
int _cl_no_rx = 0;
int _cl_rx_delay = 0;

// Module variables
unsigned char _write_count_value = 0;
unsigned char _read_count_value = 0;
int _fd = -1;

// keep our own counts for cases where the driver stats don't work
int _write_count = 0;
int _read_count = 0;
int _error_count = 0;

void dump_data(unsigned char * b, int count) {
	printf("%i bytes: ", count);
	int i;
	for (i=0; i < count; i++) {
		printf("%02x ", b[i]);
	}

	printf("\n");
}

int set_baud_divisor(int speed)
{
	// default baud was not found, so try to set a custom divisor
	struct serial_struct ss;
	if (ioctl(_fd, TIOCGSERIAL, &ss) != 0) {
		printf("TIOCGSERIAL failed\n");
		return -1;
	}

	ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
	ss.custom_divisor = (ss.baud_base + (speed/2)) / speed;
	int closest_speed = ss.baud_base / ss.custom_divisor;

	if (closest_speed < speed * 98 / 100 || closest_speed > speed * 102 / 100) {
		printf("Cannot set speed to %d, closest is %d\n", speed, closest_speed);
		exit(-1);
	}

	printf("closest baud = %i, base = %i, divisor = %i\n", closest_speed, ss.baud_base,
			ss.custom_divisor);

	ioctl(_fd, TIOCSSERIAL, &ss);
}

// converts integer baud to Linux define
int get_baud(int baud)
{
	switch (baud) {
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
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
	case 2500000:
		return B2500000;
	case 3000000:
		return B3000000;
	case 3500000:
		return B3500000;
	case 4000000:
		return B4000000;
	default: 
		return -1;
	}
}

void display_help()
{
	printf("Usage: linux-serial-test [OPTION]\n"
			"\n"
			"  -h, --help\n"
			"  -b, --baud        Baud rate, 115200, etc (115200 is default)\n"
			"  -p, --port        Port (/dev/ttyS0, etc) (must be specified)\n"
			"  -d, --divisor     UART Baud rate divisor (can be used to set custom baud rates\n"
			"  -R, --rx_dump     Dump Rx data\n"
			"  -T, --detailed_tx Detailed Tx data\n"
			"  -s, --stats       Dump serial port stats every 5s\n"
			"  -S, --stop-on-err Stop program if we encounter an error\n"
			"  -y, --single-bype Send specified byte to the serial port \n"
			"  -z, --second-bype Send another specified byte to the serial port \n"
			"  -c, --rts-cts     Enable RTS/CTS flow control \n"
			"  -e, --dump-err    Display errors \n"
			"  -r, --no-rx       Don't receive data (can be used to test flow control\n"
		        "                    when serial driver buffer is full\n"
			"  -l, --rx-delay    Delay between reading data (can be used to test flow control\n"
	      );
	exit(0);
}

void process_options(int argc, char * argv[])
{
	for (;;) {
		int option_index = 0;
		static const char *short_options = "b:p:d:RTsSy:z:cer";
		static const struct option long_options[] = {
			{"help", no_argument, 0, 0},
			{"baud", required_argument, 0, 'b'},
			{"port", required_argument, 0, 'p'},
			{"divisor", required_argument, 0, 'd'},
			{"rx_dump", no_argument, 0, 'R'},
			{"detailed_tx", no_argument, 0, 'T'},
			{"stats", no_argument, 0, 's'},
			{"stop-on-error", no_argument, 0, 'S'},
			{"timing-byte", no_argument, 0, 'a'},
			{"single-bype", no_argument, 0, 'z'},
			{"rts-cts", no_argument, 0, 'c'},
			{"no-rx", no_argument, 0, 'r'},
			{0,0,0,0},
		};

		int c = getopt_long(argc, argv, short_options,
				long_options, &option_index);

		if (c == EOF) {
			break;
		}

		switch (c) {
		case 0:
			display_help();
			break;
		case 'h':
			display_help();
			break;
		case 'b':
			_cl_baud = atoi(optarg);
			break;
		case 'p':
			strncpy(_cl_port, optarg, sizeof(_cl_port));
			break;
		case 'd':
			_cl_divisor = atoi(optarg);
			break;
		case 'R':
			_cl_rx_dump = 1;
			break;
		case 'T':
			_cl_tx_detailed = 1;
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
		case 'e':
			_cl_dump_err = 1;
			break;
		case 'r':
			_cl_no_rx = 1;
			break;
		}
	}
}

void dump_serial_port_stats()
{
	struct serial_icounter_struct icount = {};

	printf("%s: count for this session: rx=%i, tx=%i, rx err=%i\n", _cl_port, _read_count, _write_count, _error_count);

	int ret = ioctl(_fd, TIOCGICOUNT, &icount);
	if (ret != -1) {
		printf("%s: TIOCGICOUNT: ret=%i, rx=%i, tx=%i, frame = %i, overrun = %i, parity = %i, brk = %i, buf_overrun = %i\n",
				_cl_port, ret, icount.rx, icount.tx, icount.frame, icount.overrun, icount.parity, icount.brk,
				icount.buf_overrun);
	}
}

void process_read_data()
{
	unsigned char rb[30];
	int c = read(_fd, &rb, sizeof(rb));
	if (c > 0) {
		if (_cl_rx_dump)
			dump_data(rb, c);

		// verify read count is incrementing
		int i;
		for (i = 0; i < c; i++) {
			if (rb[i] != _read_count_value) {
				if (_cl_dump_err) {
					printf("Error, count: %i, expected %02x, got %02x\n",
							_read_count + i, _read_count_value, rb[i]);
				}
				_error_count++;
				if (_cl_stop_on_error) {
					dump_serial_port_stats();
					exit(-1);
				}
				_read_count_value = rb[i];
			}
			_read_count_value++;
		}
		_read_count += c;
	}
}

void process_write_data()
{
	int count = 0;
	unsigned char write_data[1024] = {0};

	while (1) {
		int i;
		for (i = 0; i < sizeof(write_data); i++) {
			write_data[i] = _write_count_value;
			_write_count_value++;
		}

		int c = write(_fd, &write_data, sizeof(write_data));

		if (c > 0) {
			_write_count += c;
			count += c;
		}

		if (c < sizeof(write_data)) {
			_write_count_value -= sizeof(write_data) - c;
			if (_cl_tx_detailed)
				printf("wrote %i bytes\n", count);
			break;
		} else {
			count += c;
		}
	}
}


void setup_serial_port(int baud)
{
	struct termios newtio;

	_fd = open(_cl_port, O_RDWR | O_NONBLOCK);

	if (_fd < 0) {
		printf("Error opening serial port \n");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */

	/* man termios get more info on below settings */
	newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;

	if (_cl_rts_cts) {
		newtio.c_cflag |= CRTSCTS;
	}

	newtio.c_iflag = 0;
	newtio.c_oflag = 0;
	newtio.c_lflag = 0;

	// block for up till 128 characters
	newtio.c_cc[VMIN] = 128;

	// 0.5 seconds read timeout
	newtio.c_cc[VTIME] = 5;

	/* now clean the modem line and activate the settings for the port */
	tcflush(_fd, TCIFLUSH);
	tcsetattr(_fd,TCSANOW,&newtio);
}

int main(int argc, char * argv[])
{
	printf("Linux serial test app\n");

	process_options(argc, argv);

	if (_cl_port[0] == 0) {
		printf("ERROR: Port argument required\n");
		display_help();
	}

	int baud = B115200;

	if (_cl_baud)
		baud = get_baud(_cl_baud);

	if (baud <= 0) {
		printf("NOTE: non standard baud rate, trying custom divisor\n");
		baud = B38400;
		setup_serial_port(B38400);
		set_baud_divisor(_cl_baud);
	} else {
		setup_serial_port(baud);
	}

	if (_cl_single_byte >= 0) {
		unsigned char data[2];
		data[0] = (unsigned char)_cl_single_byte;
		if (_cl_another_byte < 0) {
			write(_fd, &data, 1);
		} else {
			data[1] = _cl_another_byte;
			write(_fd, &data, 2);
		}
		return 0;
	}

	struct pollfd serial_poll;
	serial_poll.fd = _fd;
	if (_cl_no_rx) 
		serial_poll.events = POLLOUT;
	else
		serial_poll.events = POLLIN|POLLOUT;

	struct timespec last_stat;

	clock_gettime(CLOCK_MONOTONIC, &last_stat);

	while (1) {
		usleep(10*1000);
		int retval = poll(&serial_poll, 1, 10000);

		if (retval == -1) {
			perror("poll()");
		} else if (retval) {
			if (serial_poll.revents & POLLIN) {
				process_read_data();
			}

			if (serial_poll.revents & POLLOUT) {
				process_write_data();
			}
		} else {
			printf("No data within ten seconds.\n");
		}

		if (_cl_stats) {
			struct timespec current;
			clock_gettime(CLOCK_MONOTONIC, &current);
			if (current.tv_sec - last_stat.tv_sec > 5) {
				dump_serial_port_stats();
				last_stat = current;
			}
		}
	}
}


