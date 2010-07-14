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

int main(int argc, char *argv[]) {
	int fd, state, last, len;
	struct mq_attr q_attr = {
		mq_flags: O_NONBLOCK,
		mq_maxmsg: 4096,
		mq_msgsize: sizeof(pulse_t)
	};
	char q_name[257];
	pulse_t pulse;
	mqd_t q;
#if 0
	pid_t pid;
#endif

	if (argc != 3) {
		printf("Usage: %s <device> <meter>\n", argv[0]);
		return 1;
	}

	fd = open(argv[1], O_RDONLY|O_NONBLOCK);
	cerror(argv[1], fd < 0);

	cerror("Failed to get serial IO status", ioctl(fd, TIOCMGET, &state) != 0);
	state |= SERIO_OUT;
	cerror("Failed to set serial IO status", ioctl(fd, TIOCMSET, &state) != 0);

#if 0
	pid = fork();
	cerror("Failed to become a daemon", pid < 0);
	if (pid)
		exit(EXIT_SUCCESS);
	close(0);
	close(1);
	close(2);
#endif

	len = snprintf(q_name, sizeof(q_name), "/gasmeter_pulsemon_%zu_%lu", getuid(), strtoul(argv[2], NULL, 16));
	cerror("Failed to create message queue name", len < 0);
	cerror("Generated message queue name is too long", len > 255);

	q = mq_open(q_name, O_WRONLY|O_NONBLOCK|O_CREAT, S_IRUSR|S_IWUSR, &q_attr);
	cerror(q_name, q < 0);

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
