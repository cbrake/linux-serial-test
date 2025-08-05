// SPDX-License-Identifier: MIT

#include <stdio.h>		/* perror(), printf() */
#include <errno.h>		/* errno */
#include <stdlib.h>		/* exit() */

/* As of 2025's GNU/Linux's serial support is half-baked twice over.
 *
 *      1. termios (for tcgetattr, tcflush and friends), and
 *      2. termios2 (for picking an arbitrary baudrate), 
 *
 *   Both are often needed, but that requires contortions as termios2
 *   is defined in <asm/termios.h> which conflicts with <termios.h>.
 *   The most reasonable solution currently is to isolate termios2
 *   into its own file ("compilation unit").
*/
#include <asm/termios.h>

/*****************************************************************
 * <asm/termios.h> also conflicts with <sys/ioctl.h>, which seems a
 * bit bizarre for an interface that requires the use of ioctl(). so the
 * following prototype for ioctl() was extracted from ioctl.h.
 *****************************************************************/
/* Perform the I/O control operation specified by REQUEST on FD.
   One argument may follow; its presence and type depend on REQUEST.
   Return value depends on REQUEST.  Usually -1 indicates error.  */
#include <features.h>
#ifndef __USE_TIME64_REDIRECTS
extern int ioctl (int __fd, unsigned long int __request, ...) __THROW;
#else
# ifdef __REDIRECT
extern int __REDIRECT_NTH (ioctl, (int __fd, unsigned long int __request, ...),
			   __ioctl_time64);
# else
extern int __ioctl_time64 (int __fd, unsigned long int __request, ...) __THROW;
#  define ioctl __ioctl_time64
# endif
#endif



/* setserial's TIOCSSERIAL with custom_divisor is deprecated. Use termios2, if possible. */
int set_custom_baud(int fd, int speed)
{
	// speed was not one of the POSIX baudrates, so set it directly with termios2.
	struct termios2 tio;

	if (ioctl(fd, TCGETS2, &tio) < 0) {
		int ret = -errno;
		perror("TCGETS2 failed");
		return ret;
	}		
	tio.c_cflag &= ~CBAUD;
	tio.c_cflag |= BOTHER;
	tio.c_ispeed = tio.c_ospeed = speed; 
	if (ioctl(fd, TCSETS2, &tio) < 0) {
		int ret = -errno;
		perror("TCSETS2 setting baudrate");
		return ret;
	}
	return 0;
}

