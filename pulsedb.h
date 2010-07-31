#define xerror(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define cerror(msg, expr) do { if (expr) xerror(msg); } while(0)

#define tv_to_ull(x) (unsigned long long)((unsigned long long)(x).tv_sec*1000000 + (unsigned long long)(x).tv_usec)

/* 10ms / 10000µs */
#define MIN_PULSE 10000

#ifdef FORK
# undef VERBOSE
#endif

#ifdef VERBOSE
# define _printf(...) printf(__VA_ARGS__)
#else
# define _printf(...) do { } while(0)
#endif

void pulse_meter(const char *value);
bool pulse_on(const struct timeval *on);
bool pulse_off(const struct timeval *on, const struct timeval *off);
bool pulse_on_off(const struct timeval *on, const struct timeval *off);
bool pulse_cancel(const struct timeval *on);
