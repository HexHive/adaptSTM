#ifndef XXSTM_H
#define XXSTM_H

#include <stdint.h>
#include <setjmp.h>

// typedef uint32_t stm_word_t;
typedef intptr_t stm_word_t;

/*************************************************************************
 * Buffer definitions
 *************************************************************************/

/* one lock entry in the buffer */
typedef struct lockset {
    stm_word_t *lock;
    stm_word_t version;
} __attribute__ ((packed)) lockset_t;

/* one read entry in the buffer */
typedef struct readset {
    stm_word_t *lock;
    stm_word_t version;
} __attribute__ ((packed)) readset_t;

// number of read or lock entries in a set (to start with)
#define NRRLENTRIESINSET 64

/* one write entry in the buffer */
typedef struct writeset {
    stm_word_t *addr;
    stm_word_t value;
    //stm_word_t version; /* version == 0 if we already have that lock */
    struct writeset *next;
} __attribute__ ((packed)) writeset_t;


//#define NRLOCKSINSLAB 45
//#define NRREADSINSLAB 45
#define NRWRITESINSLAB 64 /*was 30*/
/* a slab which contains 10 read or 7 write or 15 lock entries */
typedef struct bufferslab {
    union {
	//readset_t reads[NRREADSINSLAB]; /* 15*8 = 120 - 8b left */
	writeset_t writes[NRWRITESINSLAB]; /* 7*12 = 112 - 16b left */
	//lockset_t locks[NRLOCKSINSLAB]; /* 15*8 = 120 - 8b left */
    } data;
    stm_word_t size;
    struct bufferslab *next;
} __attribute__ ((packed)) bufferslab_t;

#define SIZEOFSLAB sizeof(bufferslab_t)
#define NRSLABSPERALLOC 4


/*************************************************************************
 * Memory manager definitions
 *************************************************************************/

/* Block of allocated memory */
typedef struct mem_block {
        void *addr;                     /* Address of memory */
        struct mem_block *next;         /* Next block */
} mem_block_t;

mem_block_t *allocated;

/* Allocated but unused tx descriptors */
typedef struct tx_block {
        void *tx;                     /* Address of memory */
        struct tx_block *next;         /* Next block */
} tx_block_t;

pthread_mutex_t unused_tx_mutex;
tx_block_t *unused_tx;

/*************************************************************************
 * Lock manager definitions
 *************************************************************************/
#define NUM_BITS_FOR_HASH 22
#define LOCK_HASH_ARRAY_SIZE (1 << NUM_BITS_FOR_HASH)
#define LOCK_SHIFT 5 /* for word based locking */
#define LOCK_MASK (LOCK_HASH_ARRAY_SIZE - 1)
#define LOCK_FREE_MASK 0x01 /* 1 bit */

#define MAX_NUM_YIELD_PER_LOCK 4 /* spin x times before a retry */

#define LOCK_FREE_MASX 0x1
#define LOCK_FREE 0x1UL

//volatile stm_word_t locks[LOCK_HASH_ARRAY_SIZE];
volatile stm_word_t *locks;

#define LOCK_IDX_FROM_ADDR(addr) (((stm_word_t)addr >> LOCK_SHIFT) & LOCK_MASK)
#define ADDR2LOCKADDR(addr) (locks + LOCK_IDX_FROM_ADDR(addr))
#define LOCK_GET_VALUE(addr) ((stm_word_t)*(addr)) /* get the value of the lock */
#define LOCK_GET_VERSION_FROM_VALUE(lockValue) ((stm_word_t)lockValue) /* Get version */
#define LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) ((stm_tx_t*)lockValue) /* Get address of owner */
#define LOCK_SET_VERSION(lockAddr, v) (*lockAddr=v) /* NO CAS - because it's locked */

#define LOCK_RELEASE(lockAddr, version) LOCK_SET_VERSION(lockAddr, version)

#define LOCK_IS_FREE(lockValue) (lockValue & LOCK_FREE_MASK) /* is lock free? */

#define LOCK_SET_OWNER_ADDR(lockAddr, old, new) (CAS(lockAddr, old, new)) /* CAS to prevent race conditions */


/*************************************************************************
 * buffer dependent definitions
 *************************************************************************/
#define NUM_BITS_FOR_WBUFHASH 5
/* size in nr entries, not bytes! */
#define WBUF_HASH_ARRAY_SIZE (1 << NUM_BITS_FOR_WBUFHASH)
#define WBUF_MAX_HASH_ARRAY_SIZE (1 << (NUM_BITS_FOR_WBUFHASH*2)) /* max size is 1024 entries */

#define WBUF_SHIFT LOCK_SHIFT /* for same hash line opts */
//#define WBUF_MASK (WBUF_HASH_ARRAY_SIZE - 1)
#define WBUF_MASK (tx->whashmask)
#define WBUF_IDX_FROM_ADDR(tx, addr) (((stm_word_t)addr >> WBUF_SHIFT) & WBUF_MASK)
// TODO play with hash function!
//#define WBUF_IDX_FROM_ADDR(addr) ((((stm_word_t)addr >> 16)^((stm_word_t)addr>>5)) & WBUF_MASK)
#ifdef ADAPTIVEWHASH2
#define ADDR2WENTRY(tx, buf, addr) (buf + (((stm_word_t)addr >> tx->adaptive_hash) & WBUF_MASK))
#else
#define ADDR2WENTRY(tx, buf, addr) (buf + wbuf_idx_from_addr(tx, addr))
#endif

#define NRWBEFOREHASH 10 /* nrwbeforehash+1 are written before we extend to a hashmap */
//#define WBLOOMHASH(addr) ((addr>>LOCK_SHIFT)^(addr<<NUM_BITS_FOR_HASH))
//#define WBLOOMHASH(addr) (addr)
#define WBLOOMHASH(addr) (1 << ((((stm_word_t)addr>>3)^((stm_word_t)addr>>5)) & 0x3F))

/*************************************************************************
 * Global version counter definitions
 *************************************************************************/
//static volatile stm_word_t gclock[1024 / sizeof(stm_word_t)];
#ifdef GLOBALCLOCKCACHELINE
static volatile stm_word_t gv_counter[256];
#define GLOBAL_VERSION gv_counter[127]
#else
#define GLOBAL_VERSION gv_counter
volatile stm_word_t gv_counter;
#endif
#define GLOBAL_VERSION_INC (FETCH_ADD(&GLOBAL_VERSION,2))


/*************************************************************************
 * transaction struct definitions
 *************************************************************************/

/* Transaction status */
enum {
	TX_IDLE = 0,
	TX_ACTIVE = 1,
	TX_COMMITTED = 2,
	TX_ABORTED = 3,
	TX_WAITING = 4
};

/* transaction struct */
typedef struct stm_tx {
    stm_word_t status;					/* Transaction status (not read by other threads) */
    stm_word_t max_version;				/* Max version which may be read without extending the readset */
    //readset_t **readhash;				/* Hash table for tx local reads */
    //stm_word_t readbloom;				/* Bloom filter for tx local reads (if bloom hit -> search table) */
    writeset_t **writehash;				/* Hash table for tx local writes */
    long long writebloom;				/* Bloom filter for tx local writes (if bloom hit -> search table) */
    unsigned long nr_uniq_writes;			/* nr of tx local writes (hash table is only used if large enough) */

    bufferslab_t *writeset;                             /* allocated write slabs for this transaction */

    lockset_t *lockset;
    stm_word_t nrlocks;
    stm_word_t maxlocks;

    readset_t *readset;
    stm_word_t nrreads;
    stm_word_t maxreads;

    stm_word_t writesize, locksize, readsize;
    stm_word_t whashsize;
    stm_word_t whashmask;
    unsigned long wtotal, nrtx;
    unsigned long adaptretries, adaptcommits;		/* variables for adaptiveness */
    unsigned long whashcollisions;
    
    stm_word_t adaptive_hash;
    stm_word_t writethrough;
    
    bufferslab_t *freeslabs;				/* amount of free slabs for this tx */
    
    //bufferslab_t *lockset;                              /* allocated lock slabs for this transaction */
    //bufferslab_t *readset;                              /* allocated read slabs for this transaction */
    mem_block_t *buffers;				/* allocated rw buffers - freed when transaction is freed */
    
    mem_block_t *allocated;				/* Memory allocated by this transation (freed upon abort) */
    mem_block_t *freed;					/* Memory freed by this transation (freed upon commit) */

    struct stm_tx *waiting_for;				/* Current transaction is waiting for transaction */
    unsigned int yielded;				/* Counter to count how often a transaction has sleept while waiting for a lock */

    //void *begin_stack;					/* The following two pointers depict the stack region of the current */
    //void *end_stack;					/* Transaction. Memory accesses in this area not buffered */
    
    //    void *saved_begin_stack;            /* The following two pointers depict the stack region that must be saved and */
    //    void *saved_end_stack;              /* restored on retry. Usually part of the above transactional stack region. */
    //    void *saved_stack_copy;			    /* Pointer to the copy of this stack region. */
    
    
    jmp_buf env;					/* Environment for setjmp/longjmp */
    //jmp_buf *jmp;					/* Pointer to environment (NULL when not using setjmp/longjmp) */

#if defined(STATS) || defined(GLOBAL_STATS)
    stm_word_t start;					/* Start timestamp */
    unsigned long retries;				/* Retries of the same transaction */
    unsigned long aborts;				/* Total number of aborts (cumulative) */
    unsigned long commits;				/* Total number of commits */
#endif
#ifdef STATS
    //    unsigned long nb_writes;             /* Total number of writes */
    unsigned long nb_reads;              /* Total number of reads  */
    unsigned long nb_writes;
    
    unsigned long nb_locks;
    unsigned long nb_min_reads;
    unsigned long nb_max_reads;
    unsigned long nb_min_writes;
    unsigned long nb_max_writes;
    unsigned long nb_tot_reads;
    unsigned long nb_tot_writes;

    unsigned long nb_read_ver_err;
    unsigned long nb_read_ver_err_rec;

    unsigned long nb_lock_ver_err;
    unsigned long nb_lock_ver_err_rec;

    unsigned long nb_read_ver_change;
#endif
} stm_tx_t;

#ifdef GLOBAL_STATS
unsigned long xxstm_nr_tx = 0;
#endif

/*************************************************************************
 * static assertions and CAS implementation
 *************************************************************************/

/**
 * Check the correctness of the structure offset constants at compile time by the C compiler.
 */
#define static_assert(cond)  do { enum { static_assert_failed = 1/(cond) }; } while (0)

inline static void static_assert_structure_offsets() {
        static_assert(SIZEOFSLAB >= sizeof(bufferslab_t));
	static_assert(NRWBEFOREHASH<=NRWRITESINSLAB);
}

/*************************************************************************
 * function declarations
 *************************************************************************/
#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)
      
/* global functions */
void stm_init();
void stm_exit();
jmp_buf *stm_get_env(stm_tx_t *tx);

stm_tx_t *stm_new();
void stm_delete(stm_tx_t *tx);

void stm_commit(stm_tx_t *tx);
inline void stm_retry(stm_tx_t *tx);

void stm_start(stm_tx_t *tx, jmp_buf *env);

stm_word_t stm_load(stm_tx_t *tx, stm_word_t *addr);
void stm_store(stm_tx_t *tx, stm_word_t *addr, stm_word_t value);

void *stm_malloc(stm_tx_t *tx, size_t size);
void stm_free(stm_tx_t *tx, void *addr);

#endif // define xxstm.h
