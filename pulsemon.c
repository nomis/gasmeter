#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "pulsemon.h"

#define tv_to_ull(x) (unsigned long long)((unsigned long long)(x).tv_sec*1000000 + (unsigned long long)(x).tv_usec)

/* Some code copied from taylor-uucp. */

#define ICLEAR_IFLAG (BRKINT | ICRNL | IGNBRK | IGNCR | IGNPAR \
	| INLCR | INPCK | ISTRIP | IXOFF | IXON \
	| PARMRK | IMAXBEL)
#define ICLEAR_OFLAG (OPOST)
#define ICLEAR_CFLAG (CSIZE | PARENB | PARODD | HUPCL)
#define ISET_CFLAG (CS8 | CREAD | CLOCAL)
#define ICLEAR_LFLAG (ECHO | ECHOE | ECHOK | ECHONL | ICANON | IEXTEN \
	| ISIG | NOFLSH | TOSTOP | PENDIN | CRTSCTS)

/* ---- */

int main(int argc, char *argv[]) {
	int fd, state, last, iflags;
	struct termios ios;
#if 0
	pid_t pid;
#endif

	if (argc != 2) {
		printf("Usage: %s <device>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY|O_NONBLOCK);
	cerror(argv[1], fd < 0);

	iflags = fcntl(fd, F_GETFL, 0);
	cerror("Failed to get file descriptor flags for opened serial port", iflags < 0);
	iflags &= ~(O_NONBLOCK|O_NDELAY);
	iflags = fcntl(fd, F_SETFL, iflags);
	cerror("Failed to set file descriptor flags", (iflags & O_NONBLOCK) != 0);
	cerror("Failed to get terminal attributes", tcgetattr(fd, &ios));
	
	ios.c_iflag &=~ ICLEAR_IFLAG;
	ios.c_oflag &=~ ICLEAR_OFLAG;
	ios.c_cflag &=~ ICLEAR_CFLAG;
	ios.c_cflag |= ISET_CFLAG;
	ios.c_lflag &=~ ICLEAR_LFLAG;
	ios.c_cc[VMIN] = 1;
	ios.c_cc[VTIME] = 0;
	cfsetispeed(&ios, B38400);
	cfsetospeed(&ios, B38400);

	cerror("Failed to flush terminal input", ioctl(fd, TCFLSH, 0) < 0);
	cerror("Failed to set terminal attributes", tcsetattr(fd, TCSANOW, &ios));

#if 0
	cerror("Failed to get serial IO status", ioctl(fd, TIOCMGET, &state) != 0);
	status |= SERIO_OUT;
	cerror("Failed to set serial IO status", ioctl(fd, TIOCMSET, &state) != 0);

	pid = fork();
	cerror("Failed to become a daemon", pid < 0);
	if (pid)
		exit(EXIT_SUCCESS);
	close(0);
	close(1);
	close(2);
#endif

	last = ~0;
	do {
		struct timeval tv;

		cerror("Failed to get serial IO status", ioctl(fd, TIOCMGET, &state) != 0);
		state &= SERIO_IN;

		if (last != state) {
			gettimeofday(&tv, NULL);
			printf("%lu.%06lu: %d\n", tv.tv_sec, tv.tv_usec, state != 0);
		}

		last = state;
	} while (ioctl(fd, TIOCMIWAIT, SERIO_IN) == 0);
	cerror("Failed to close serial device", close(fd));
	xerror("Failed to get serial IO status");
}
