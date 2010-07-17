#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pulsemon.h"
#include "pulseq.h"

int main(int argc, char *argv[]) {
	int fd, state, last;
	struct mq_attr q_attr = {
		mq_flags: O_NONBLOCK,
		mq_maxmsg: 4096,
		mq_msgsize: sizeof(pulse_t)
	};
	pulse_t pulse;
	mqd_t q;
#if 0
	pid_t pid;
#endif

	if (argc != 3) {
		printf("Usage: %s <device> <mqueue>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY|O_NONBLOCK);
	cerror(argv[1], fd < 0);

#if SERIO_OUT != 0
	cerror("Failed to get serial IO status", ioctl(fd, TIOCMGET, &state) != 0);
	state |= SERIO_OUT;
	cerror("Failed to set serial IO status", ioctl(fd, TIOCMSET, &state) != 0);
#endif

	q = mq_open(argv[2], O_WRONLY|O_NONBLOCK|O_CREAT, S_IRUSR|S_IWUSR, &q_attr);
	cerror(argv[2], q < 0);

#if 0
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
		cerror("Failed to get serial IO status", ioctl(fd, TIOCMGET, &state) != 0);
		state &= SERIO_IN;

		if (last != state) {
			gettimeofday(&pulse.tv, NULL);
			pulse.on = (state != 0);
#if 0
			printf("%lu.%06lu: %d\n", pulse.tv.tv_sec, pulse.tv.tv_usec, pulse.on);
#endif
			mq_send(q, (const char *)&pulse, sizeof(pulse), 0);
		}

		last = state;
	} while (ioctl(fd, TIOCMIWAIT, SERIO_IN) == 0);
	cerror("Failed to close serial device", close(fd));
	xerror("Failed to wait for serial IO status");
}
