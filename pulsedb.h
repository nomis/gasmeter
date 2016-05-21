#define xerror(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define cerror(msg, expr) do { if (expr) xerror(msg); } while(0)

#define tv_to_ull(x) (unsigned long long)((unsigned long long)(x).tv_sec*1000000 + (unsigned long long)(x).tv_usec)

/* Avoid false pulses caused by electricity noise at 50/60Hz
 * 1s / 50Hz + 5% = 21000µs
 * 1s / 60Hz + 5% = 17500µs
 */
#define MIN_PULSE 21000

#ifdef FORK
# ifndef SYSLOG
#  define SYSLOG
# endif
#endif

#ifdef VERBOSE
# if SYSLOG
#  define _printf(...) syslog(LOG_INFO, __VA_ARGS__)
# else
#  define _printf(...) printf(__VA_ARGS__)
# endif
#else
# define _printf(...) do { } while(0)
#endif

void pulse_meter(const char *value);
bool pulse_on(const struct timeval *on);
bool pulse_off(const struct timeval *on, const struct timeval *off);
bool pulse_on_off(const struct timeval *on, const struct timeval *off);
bool pulse_cancel(const struct timeval *on);
bool pulse_resume(const struct timeval *on);
bool pulse_reset(void);
