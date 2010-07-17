#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pulsedb.h"
#include "pulseq.h"

int waiting_sig;

void handle_signal(int sig) {
	if (waiting_sig == 0)
		waiting_sig = sig;
}

int main(int argc, char *argv[]) {
	int ret, i, count = 0;
	bool process_on = 1;
	struct mq_attr qmain_attr = {
		mq_flags: 0,
		mq_maxmsg: 4096,
		mq_msgsize: sizeof(pulse_t)
	};
	struct mq_attr qbackup_attr = {
		mq_flags: 0,
		mq_maxmsg: 2,
		mq_msgsize: sizeof(pulse_t)
	};
	struct sigaction sa_ign = {
		sa_handler: handle_signal,
		sa_flags: 0
	};
	struct sigaction sa_dfl = {
		sa_handler: SIG_DFL,
		sa_flags: 0
	};
	sigset_t die_signals;
	pulse_t pulse[2];
	mqd_t qmain, qbackup;
#if 0
	pid_t pid;
#endif

	if (argc != 3) {
		printf("Usage: %s <mqueue> <meter>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	qmain = mq_open(argv[1], O_RDONLY|O_CREAT, S_IRUSR|S_IWUSR, &qmain_attr);
	cerror(argv[1], qmain < 0);

	{
		char *backup = malloc(strlen(argv[1]) * sizeof(char) + 2);
		cerror("malloc", backup == NULL);

		ret = sprintf(backup, "%s~", argv[1]);
		cerror("snprintf", ret < 0);

		qbackup = mq_open(backup, O_RDWR|O_NONBLOCK|O_CREAT, S_IRUSR|S_IWUSR, &qbackup_attr);
		cerror(backup, qbackup < 0);

		free(backup);
	}

#if 0
	pid = fork();
	cerror("Failed to become a daemon", pid < 0);
	if (pid)
		exit(EXIT_SUCCESS);
	close(0);
	close(1);
	close(2);
#endif

	sigemptyset(&die_signals);
	sigaddset(&die_signals, SIGINT);
	sigaddset(&die_signals, SIGQUIT);
	sigaddset(&die_signals, SIGTERM);

	sa_ign.sa_mask = die_signals;
	sa_dfl.sa_mask = die_signals;

	sigprocmask(SIG_BLOCK, &die_signals, NULL);

	/* read from backup queue */
	while (count < 2) {
		ret = mq_receive(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
		if (ret != sizeof(pulse_t)) {
			cerror("mq_receive backup", errno != EAGAIN);
			break;
		} else {
			_printf("read %d %lu.%06lu %d from backup queue\n", count, pulse[count].tv.tv_sec, pulse[count].tv.tv_usec, pulse[count].on);
			count++;
		}
	}

	/* discard unknown off pulse */
	if (count == 1 && !pulse[0].on)
		count = 0;

	/* write to backup queue */
	for (i = count - 1; i >= 0; i--) {
		ret = mq_send(qbackup, (char *)&pulse[i], sizeof(pulse_t), 0);
		cerror("mq_send backup", ret != 0);
		_printf("wrote %d %lu.%06lu %d to backup queue\n", i, pulse[i].tv.tv_sec, pulse[i].tv.tv_usec, pulse[i].on);
	}

	sigprocmask(SIG_UNBLOCK, &die_signals, NULL);

	do {
		_printf("main loop %d\n", count);
		assert(count >= 0);
		assert(count <= 2);
		switch (count) {
		case 2: /* pulse on, pulse off */
			assert(pulse[0].on);
			assert(!pulse[1].on);

			if (process_on) {
				/* TODO process on+off pulse */
				_printf("process on+off pulse\n");
			} else {
				/* TODO process off pulse */
				_printf("process off pulse\n");
			}

			/* clear backup queue */
			_printf("clearing backup queue\n");
			ret = mq_receive(qbackup, (char *)&pulse[1], sizeof(pulse_t), 0);
			cerror("mq_receive backup", ret != sizeof(pulse_t));
			ret = mq_receive(qbackup, (char *)&pulse[0], sizeof(pulse_t), 0);
			cerror("mq_receive backup", ret != sizeof(pulse_t));
			count = 0;
			break;

		case 1: /* pulse on */
			assert(pulse[0].on);
			if (process_on) {
				/* TODO process on pulse */
				_printf("process on pulse\n");
				process_on = 0;
			}

		case 0: /* no data */
			waiting_sig = 0;
			sigaction(SIGINT, &sa_ign, NULL);
			sigaction(SIGQUIT, &sa_ign, NULL);
			sigaction(SIGTERM, &sa_ign, NULL);

			ret = mq_receive(qmain, (char *)&pulse[count], sizeof(pulse_t), 0);
			if (ret != sizeof(pulse_t)) {
				cerror("mq_receive main", errno != EINTR);
				xerror("mq_receive main");
			} else {
				sigprocmask(SIG_BLOCK, &die_signals, NULL);

				_printf("read %d %lu.%06lu %d from main queue\n", count, pulse[count].tv.tv_sec, pulse[count].tv.tv_usec, pulse[count].on);

				if (count == 0) {
					if (pulse[count].on) { /* new on pulse */
						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06lu %d to backup queue\n", count, pulse[count].tv.tv_sec, pulse[count].tv.tv_usec, pulse[count].on);

						count++;
						process_on = 1;
					} /* discard unknown off pulse */
				} else {
					if (!pulse[count].on) { /* matching off pulse */
						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06lu %d to backup queue\n", count, pulse[count].tv.tv_sec, pulse[count].tv.tv_usec, pulse[count].on);

						count++;
					} else { /* duplicate on pulse */
						/* clear backup queue */
						_printf("clearing backup queue\n");
						ret = mq_receive(qbackup, (char *)&pulse[0], sizeof(pulse_t), 0);
						cerror("mq_receive backup", ret != sizeof(pulse_t));

						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06lu %d to backup queue\n", count, pulse[count].tv.tv_sec, pulse[count].tv.tv_usec, pulse[count].on);

						pulse[0] = pulse[1];
						process_on = 1;
					}
				}
			}

			sigprocmask(SIG_UNBLOCK, &die_signals, NULL);
			sigaction(SIGINT, &sa_dfl, NULL);
			sigaction(SIGQUIT, &sa_dfl, NULL);
			sigaction(SIGTERM, &sa_dfl, NULL);

			if (waiting_sig != 0)
				kill(getpid(), waiting_sig);
			break;
		}
	} while (1);
	exit(EXIT_FAILURE);
}
