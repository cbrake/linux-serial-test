#ifndef SETBAUDRATE
#define SETBAUDRATE

// speed_t is defined via asm/termios.h to be unsigned int.
int set_custom_baud(int fd, speed_t speed);

#endif//SETBAUDRATE
