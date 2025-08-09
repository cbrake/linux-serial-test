#ifndef SETBAUDRATE
#define SETBAUDRATE

// speed_t is defined via asm/termios.h to be unsigned int.
int bother_set_baud(int fd, speed_t speed);
speed_t bother_get_baud(int fd);

#endif//SETBAUDRATE
