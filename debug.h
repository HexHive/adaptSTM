#ifdef DEBUG
#define DEBUG_OUTPUT_FILE "/tmp/xxstm.out"
pthread_mutex_t debug_mutex;
pthread_key_t thread_debug;
FILE* debugStream;
volatile int dodebug=1;
#define DEBUG_START_IMPL \
do { \
debugStream = fopen(DEBUG_OUTPUT_FILE,"w"); \
pthread_mutex_init(&debug_mutex, NULL); \
} while(0);

#define DEBUG_END_IMPL \
do { \
pthread_mutex_destroy(&debug_mutex); \
fclose(debugStream); \
debugStream = (void *) 0; \
} while(0);

#define DPRINTF(...) \
do { \
    if (dodebug==1) {		  \
pthread_mutex_lock(&debug_mutex); \
fprintf(debugStream, __VA_ARGS__); \
fflush(debugStream); \
pthread_mutex_unlock(&debug_mutex); \
    }				    \
} while(0);

#else
#define DPRINTF(...)
#define DEBUG_START_IMPL
#define DEBUG_END_IMPL
#endif

