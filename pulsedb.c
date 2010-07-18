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

int backoff = 1;

static bool __pulse_on(const struct timeval *on, const struct timeval *off) {
	(void)off;
	return pulse_on(on);
}

static void try(bool (*func)(const struct timeval *, const struct timeval *), const pulse_t *pulse) {
	while (!func(&pulse[0].tv, &pulse[1].tv)) {
		sleep(backoff);

		if (backoff < 256)
			backoff <<= 1;
	}

	backoff = 1;
}

int main(int argc, char *argv[]) {
	unsigned long int meter;
	int ret, i, count = 0;
	bool process_on = true;
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
	struct sigaction sa_ign = { /* handle signals, but store them for later */
		sa_handler: handle_signal,
		sa_flags: 0
	};
	struct sigaction sa_dfl = { /* use default signal handler */
		sa_handler: SIG_DFL,
		sa_flags: 0
	};
	sigset_t die_signals;
	pulse_t pulse[2];
	mqd_t qmain, qbackup;
#ifdef FORK
	pid_t pid;
#endif

	if (argc != 3) {
		printf("Usage: %s <mqueue> <meter>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	errno = 0;
	meter = strtoul(argv[2], NULL, 10);
	cerror(argv[2], errno != 0);

	pulse_meter(meter);

	/* only need to care about intentional kills of
	 * the process, as anything else is unrecoverable
	 */
	cerror("sigemptyset", sigemptyset(&die_signals) != 0);
	cerror("sigaddset SIGHUP", sigaddset(&die_signals, SIGHUP) != 0);
	cerror("sigaddset SIGINT", sigaddset(&die_signals, SIGINT) != 0);
	cerror("sigaddset SIGQUIT", sigaddset(&die_signals, SIGQUIT) != 0);
	cerror("sigaddset SIGTERM", sigaddset(&die_signals, SIGTERM) != 0);

	sa_ign.sa_mask = die_signals;
	cerror("sigemptyset", sigemptyset(&sa_dfl.sa_mask) != 0);

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

#ifdef FORK
	pid = fork();
	cerror("Failed to become a daemon", pid < 0);
	if (pid)
		exit(EXIT_SUCCESS);
	close(0);
	close(1);
	close(2);
#endif

	/* critical section:
	 *
	 * block (hold) all signals
	 */
	cerror("sigprocmask SIG_BLOCK", sigprocmask(SIG_BLOCK, &die_signals, NULL) != 0);

	/* read from backup queue */
	while (count < 2) {
		ret = mq_receive(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
		if (ret != sizeof(pulse_t)) {
			cerror("mq_receive backup", errno != EAGAIN);
			break;
		} else {
			_printf("read %d %lu.%06u %d from backup queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);
			count++;
		}
	}

	/* discard unknown off pulse */
	if (count == 1 && !pulse[0].on)
		count = 0;

	/* write to backup queue */
	for (i = 0; i < count; i++) {
		ret = mq_send(qbackup, (char *)&pulse[i], sizeof(pulse_t), 0);
		cerror("mq_send backup", ret != 0);
		_printf("wrote %d %lu.%06u %d to backup queue\n", i, (unsigned long int)pulse[i].tv.tv_sec, (unsigned int)pulse[i].tv.tv_usec, pulse[i].on);
	}

	/* non-critical section:
	 *
	 * unblock all signals (receive pending signals immediately)
	 */
	cerror("sigprocmask SIG_UNBLOCK", sigprocmask(SIG_UNBLOCK, &die_signals, NULL) != 0);

	do {
		_printf("main loop %d\n", count);
		assert(count >= 0);
		assert(count <= 2);

		switch (count) {
		case 2: /* pulse on, pulse off */
			assert(pulse[0].on);
			assert(!pulse[1].on);

			if (process_on) {
				_printf("process on+off pulse\n");
				try(pulse_on_off, pulse);
			} else {
				_printf("process off pulse\n");
				try(pulse_off, pulse);
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
				_printf("process on pulse\n");
				try(__pulse_on, pulse);
				process_on = false;
			}

		case 0: /* no data */
			waiting_sig = 0;

			/* managed section:
			 *
			 * handle signals, saving them for later
			 */
			cerror("sigaction SIGHUP", sigaction(SIGHUP, &sa_ign, NULL) != 0);
			cerror("sigaction SIGINT", sigaction(SIGINT, &sa_ign, NULL) != 0);
			cerror("sigaction SIGQUIT", sigaction(SIGQUIT, &sa_ign, NULL) != 0);
			cerror("sigaction SIGTERM", sigaction(SIGTERM, &sa_ign, NULL) != 0);

			/* although the signal handler will stop
			 * signals killing the process, this will
			 * return EINTR allowing it to be killed
			 * in this non-critical section
			 */
			ret = mq_receive(qmain, (char *)&pulse[count], sizeof(pulse_t), 0);
			if (ret != sizeof(pulse_t)) {
				/* non-critical section:
				 *
				 * exit, possibly with interrupted status
				 */
				if (errno == 0)
					errno = EIO; /* message size mismatch */
				xerror("mq_receive main");
			} else {
				/* critical section:
				 *
				 * signals are currently handled, but
				 * would cause mq_send to return EINTR
				 *
				 * block (hold) all signals
				 */
				ret = sigprocmask(SIG_BLOCK, &die_signals, NULL);
				if (ret != 0) {
					perror("sigprocmask SIG_BLOCK");
					/* need to continue */
				}

				_printf("read %d %lu.%06u %d from main queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);

				if (count == 0) {
					if (pulse[count].on) { /* new on pulse */
						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06u %d to backup queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);

						count++;
						process_on = true;
					} /* discard unknown off pulse */
				} else {
					if (!pulse[count].on) { /* matching off pulse */
						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06u %d to backup queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);

						count++;
					} else { /* duplicate on pulse */
						/* clear backup queue */
						_printf("clearing backup queue\n");
						ret = mq_receive(qbackup, (char *)&pulse[0], sizeof(pulse_t), 0);
						cerror("mq_receive backup", ret != sizeof(pulse_t));

						ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
						cerror("mq_send backup", ret != 0);
						_printf("wrote %d %lu.%06u %d to backup queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);

						pulse[0] = pulse[1];
						process_on = true;
					}
				}
			}

			/* non-critical section:
			 *
			 * unblock all signals (receive pending signals immediately)
			 * resume use of default signal handlers
			 */
			cerror("sigprocmask SIG_UNBLOCK", sigprocmask(SIG_UNBLOCK, &die_signals, NULL) != 0);
			cerror("sigaction SIGHUP", sigaction(SIGHUP, &sa_dfl, NULL) != 0);
			cerror("sigaction SIGINT", sigaction(SIGINT, &sa_dfl, NULL) != 0);
			cerror("sigaction SIGQUIT", sigaction(SIGQUIT, &sa_dfl, NULL) != 0);
			cerror("sigaction SIGTERM", sigaction(SIGTERM, &sa_dfl, NULL) != 0);

			/* resend first signal captured by signal handler */
			if (waiting_sig != 0)
				cerror("kill", kill(getpid(), waiting_sig) != 0);
			break;
		}
	} while (1);
	exit(EXIT_FAILURE);
}
