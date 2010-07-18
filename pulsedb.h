#define xerror(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)
#define cerror(msg, expr) do { if (expr) xerror(msg); } while(0)

#ifdef FORK
# undef VERBOSE
#endif

#ifdef VERBOSE
# define _printf(...) printf(__VA_ARGS__)
#else
# define _printf(...) do { } while(0)
#endif

bool pulse_on(unsigned long int meter, const struct timeval *on);
bool pulse_off(unsigned long int meter, const struct timeval *on, const struct timeval *off);
bool pulse_on_off(unsigned long int meter, const struct timeval *on, const struct timeval *off);
