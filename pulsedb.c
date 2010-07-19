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

char *mqueue_main;
char *mqueue_backup;
unsigned long int meter;
mqd_t qmain, qbackup;
bool process_on = true;
pulse_t pulse[2];
int count = 0;

void handle_signal(int sig);

struct sigaction sa_ign = { /* handle signals, but store them for later */
	sa_handler: handle_signal,
	sa_flags: 0
};
struct sigaction sa_dfl = { /* use default signal handler */
	sa_handler: SIG_DFL,
	sa_flags: 0
};
sigset_t die_signals;
int waiting_sig = 0;

void handle_signal(int sig) {
	if (waiting_sig == 0)
		waiting_sig = sig;
}

static void setup(int argc, char *argv[]) {
	int ret;

	if (argc != 3) {
		printf("Usage: %s <mqueue> <meter>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	mqueue_main = argv[1];

	mqueue_backup = malloc(strlen(mqueue_main) * sizeof(char) + 2);
	cerror("malloc", mqueue_backup == NULL);

	ret = sprintf(mqueue_backup, "%s~", mqueue_main);
	cerror("snprintf", ret < 0);

	errno = 0;
	meter = strtoul(argv[2], NULL, 10);
	cerror(argv[2], errno != 0);
}

static void signal_init(void) {
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
}

static void init(void) {
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

	qmain = mq_open(mqueue_main, O_RDONLY|O_CREAT, S_IRUSR|S_IWUSR, &qmain_attr);
	cerror(mqueue_main, qmain < 0);

	qbackup = mq_open(mqueue_backup, O_RDWR|O_NONBLOCK|O_CREAT, S_IRUSR|S_IWUSR, &qbackup_attr);
	cerror(mqueue_backup, qbackup < 0);

	signal_init();
	pulse_meter(meter);
}

static void signal_hold(void) {
	cerror("sigprocmask SIG_BLOCK", sigprocmask(SIG_BLOCK, &die_signals, NULL) != 0);
}

static void try_signal_hold(void) {
	int ret = sigprocmask(SIG_BLOCK, &die_signals, NULL);
	if (ret != 0) {
		perror("sigprocmask SIG_BLOCK");
		/* need to continue */
	}
}
static void signal_dispatch(void) {
	cerror("sigprocmask SIG_UNBLOCK", sigprocmask(SIG_UNBLOCK, &die_signals, NULL) != 0);
}

static void backup_pulse(void) {
	int ret = mq_send(qbackup, (char *)&pulse[count], sizeof(pulse_t), 0);
	cerror("mq_send backup", ret != 0);
	_printf("wrote %d %lu.%06u %d to backup queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);
}

static void backup_clear(void) {
	_printf("clearing backup queue\n");
	while (count > 0) {
		int ret = mq_receive(qbackup, (char *)&pulse[--count], sizeof(pulse_t), 0);
		cerror("mq_receive backup", ret != sizeof(pulse_t));
	}	
}

static void backup_load(void) {
	int ret, loaded = 0;

	/* critical section:
	 *
	 * block (hold) all signals
	 */
	signal_hold();

	/* read from backup queue */
	while (loaded < 2) {
		ret = mq_receive(qbackup, (char *)&pulse[loaded], sizeof(pulse_t), 0);
		if (ret != sizeof(pulse_t)) {
			cerror("mq_receive backup", errno != EAGAIN);
			break;
		} else {
			_printf("read %d %lu.%06u %d from backup queue\n", loaded, (unsigned long int)pulse[loaded].tv.tv_sec, (unsigned int)pulse[loaded].tv.tv_usec, pulse[loaded].on);
			loaded++;
		}
	}

	/* discard unknown off pulse */
	if (loaded == 1 && !pulse[0].on)
		loaded = 0;

	/* write to backup queue */
	for (count = 0; count < loaded; count++)
		backup_pulse();

	/* non-critical section:
	 *
	 * unblock all signals (receive pending signals immediately)
	 */
	signal_dispatch();
}

static void daemon(void) {
#ifdef FORK
	pid_t pid = fork();
	cerror("Failed to become a daemon", pid < 0);
	if (pid)
		exit(EXIT_SUCCESS);
	close(0);
	close(1);
	close(2);
#endif
}

static bool __pulse_on(const struct timeval *on, const struct timeval *off) {
	(void)off;
	return pulse_on(on);
}

static void save(bool (*func)(const struct timeval *, const struct timeval *)) {
	int backoff = 1;

	while (!func(&pulse[0].tv, &pulse[1].tv)) {
		sleep(backoff);

		if (backoff < 256)
			backoff <<= 1;
	}
}

static void save_off(void) {
	assert(pulse[0].on);
	assert(!pulse[1].on);

	if (process_on) {
		_printf("process on+off pulse\n");
		save(pulse_on_off);
	} else {
		_printf("process off pulse\n");
		save(pulse_off);
	}

	backup_clear();
}

static void save_on(void) {
	assert(pulse[0].on);

	if (process_on) {
		_printf("process on pulse\n");
		save(__pulse_on);
		process_on = false;
	}
}

static void handle_pulse(void) {
	if (count == 0) {
		/* no data */
		if (pulse[count].on) { /* new on pulse */
			backup_pulse();

			count++;
			process_on = true;
		} else { /* discard unknown off pulse */ }
	} else {
		/* on pulse waiting for off pulse */
		assert(count == 1);

		if (!pulse[count].on) { /* matching off pulse */
			backup_pulse();

			count++;
		} else { /* duplicate on pulse */
			backup_clear();
			backup_pulse();

			pulse[0] = pulse[1];
			process_on = true;
		}
	}
}

static void signal_capture(void) {
	cerror("sigaction SIGHUP", sigaction(SIGHUP, &sa_ign, NULL) != 0);
	cerror("sigaction SIGINT", sigaction(SIGINT, &sa_ign, NULL) != 0);
	cerror("sigaction SIGQUIT", sigaction(SIGQUIT, &sa_ign, NULL) != 0);
	cerror("sigaction SIGTERM", sigaction(SIGTERM, &sa_ign, NULL) != 0);
}

static void signal_release(void) {
	cerror("sigaction SIGHUP", sigaction(SIGHUP, &sa_dfl, NULL) != 0);
	cerror("sigaction SIGINT", sigaction(SIGINT, &sa_dfl, NULL) != 0);
	cerror("sigaction SIGQUIT", sigaction(SIGQUIT, &sa_dfl, NULL) != 0);
	cerror("sigaction SIGTERM", sigaction(SIGTERM, &sa_dfl, NULL) != 0);
}

static void get_data(void) {
	int ret;

	/* managed section:
	*
	* handle signals, saving them for later
	*/
	signal_capture();

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
		try_signal_hold();

		_printf("read %d %lu.%06u %d from main queue\n", count, (unsigned long int)pulse[count].tv.tv_sec, (unsigned int)pulse[count].tv.tv_usec, pulse[count].on);
		handle_pulse();
	}

	/* non-critical section:
	*
	* unblock all signals (receive pending signals immediately)
	* resume use of default signal handlers
	*/
	signal_dispatch();
	signal_release();
}

static void loop(void) {
	do {
		_printf("main loop %d\n", count);
		assert(count >= 0);
		assert(count <= 2);

		switch (count) {
		case 2: /* pulse on, pulse off */
			save_off();
			break;

		case 1: /* pulse on */
			save_on();

		case 0: /* no data */
			get_data();
			break;
		}
	} while(waiting_sig == 0);

	/* resend first signal captured by signal handler */
	if (waiting_sig != 0)
		cerror("kill", kill(getpid(), waiting_sig) != 0);
}

static void cleanup(void) {
	cerror(mqueue_main, mq_close(qmain));
	cerror(mqueue_backup, mq_close(qbackup));
	free(mqueue_backup);
}

int main(int argc, char *argv[]) {
	setup(argc, argv);
	init();
	backup_load();
	daemon();
	loop();
	cleanup();
	exit(EXIT_FAILURE);
}
