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

void dump_data(unsigned char * b, int count) {
	printf("%i bytes: ", count);
	int i;
	for (i=0; i < count; i++) {
		printf("%02x ", b[i]);
	}

	printf("\n");
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
	      );
	exit(0);
}

int _cl_baud = 0;
char _cl_port[50] = "";
int _cl_divisor = 0;
int _cl_rx_dump = 0;
int _cl_tx_detailed = 0;
int _cl_stats = 0;

void process_options(int argc, char * argv[])
{
	for (;;) {
		int option_index = 0;
		static const char *short_options = "b:p:d:RT";
		static const struct option long_options[] = {
			{"help", no_argument, 0, 0},
			{"baud", required_argument, 0, 'b'},
			{"port", required_argument, 0, 'p'},
			{"divisor", required_argument, 0, 'd'},
			{"rx_dump", no_argument, 0, 'R'},
			{"detailed_tx", no_argument, 0, 'T'},
			{"stats", no_argument, 0, 's'},
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
		case 'D':
			_cl_rx_dump = 1;
			break;
		case 'T':
			_cl_tx_detailed = 1;
			break;
		case 's':
			_cl_stats = 1;
			break;
		}
	}
}

unsigned char _write_count_value = 0;
unsigned char _read_count_value = 0;
int _fd = -1;

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
				printf("Error, expected %02x, got %02x\n",
						_read_count_value, rb[i]);
				exit(-1);
			}
			_read_count_value++;
		}
	}
}

void process_write_data()
{
	int count = 0;
	unsigned char write_data[100] = {0};

	while (1) {
		int i;
		for (i = 0; i < sizeof(write_data); i++) {
			write_data[i] = _write_count_value;
			_write_count_value++;
		}

		int c = write(_fd, &write_data, sizeof(write_data));

		if (c > 0) {
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

void dump_serial_port_stats()
{
    struct serial_icounter_struct icount = {};
    int ret = ioctl(_fd, TIOCGICOUNT, &icount);
    if (ret == -1) {
	    printf("Error getting serial port stats\n"); 
    } else {
	    printf("%s, TIOCGICOUNT: ret=%i, rx=%i, tx=%i, frame = %i, overrun = %i, parity = %i, brk = %i, buf_overrun = %i",
		_cl_port, ret, icount.rx, icount.tx, icount.frame, icount.overrun, icount.parity, icount.brk,
		icount.buf_overrun);
    }
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
		printf("ERROR: non standard baud rate\n");
		exit(-1);
	}

	struct termios newtio;

	_fd = open(_cl_port, O_RDWR | O_NONBLOCK);

	if (_fd < 0) {
		printf("Error opening serial port \n");
		exit(-1);
	}

	bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */

	/* man termios get more info on below settings */
	newtio.c_cflag = baud | CS8 | CLOCAL | CREAD;
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

	struct pollfd serial_poll;
	serial_poll.fd = _fd;
	serial_poll.events = POLLIN|POLLOUT;

	struct timespec last_stat;

	clock_gettime(CLOCK_MONOTONIC, &last_stat);

	while (1) {
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
				clock_gettime(CLOCK_MONOTONIC, &last_stat);
			}
		}
	}
}


