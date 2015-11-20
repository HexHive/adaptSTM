/**
 * main adaptstm c file
 * Implementation of the STM logic
 *
 * Copyright (c) 2010 ETH Zurich
 *   Mathias Payer <mathias.payer@inf.ethz.ch>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <setjmp.h>
#include <assert.h>
#include <string.h>
#include <sched.h>
#include <pthread.h>
#ifndef NO_SSE
#include <emmintrin.h>
#endif

#include "debug.h"
#include "adaptstm.h"
#include "adaptstm-cas.h"
#include "adaptstm-internal.h"

static void free_tx(stm_tx_t *tx);

inline __always_inline static void sse2_memzero128aligned(void *ptr, int n)
{
    __m128d d = (__m128d)_mm_setzero_si128 ();

    assert(((stm_word_t)ptr)%16==0);
    assert(n%128==0);
    char *p, *endptr = ((char*)ptr)+n; // = ptr;
    for(p = ptr; p < endptr; p+=128) {
	_mm_stream_pd((double*)&p[0], d);
	_mm_stream_pd((double*)&p[16], d);
	_mm_stream_pd((double*)&p[32], d);
	_mm_stream_pd((double*)&p[48], d);
	_mm_stream_pd((double*)&p[64], d);
	_mm_stream_pd((double*)&p[80], d);
	_mm_stream_pd((double*)&p[96], d);
	_mm_stream_pd((double*)&p[112], d);
    }
    _mm_sfence();
}

inline static void sse2_memset128aligned(void *ptr, int n, stm_word_t word)
{
#ifdef __LP64__
    __m128d d = (__m128d)_mm_set_epi64((__m64)word, (__m64)word);
#else    
    __m128d d = (__m128d)_mm_set_epi32(word, word, word, word);
#endif
    assert(((stm_word_t)ptr)%16==0);
    assert(n%128==0);
    
    char *p, *endptr = ((char*)ptr)+n; // = ptr;
    for(p = ptr; p < endptr; p+=128) {
	_mm_stream_pd((double*)&p[0], d);
	_mm_stream_pd((double*)&p[16], d);
	_mm_stream_pd((double*)&p[32], d);
	_mm_stream_pd((double*)&p[48], d);
	_mm_stream_pd((double*)&p[64], d);
	_mm_stream_pd((double*)&p[80], d);
	_mm_stream_pd((double*)&p[96], d);
	_mm_stream_pd((double*)&p[112], d);
    }
    _mm_sfence();
}

/** allocates SIZEOFSLAB slab (128b to span 2 cachelines)
 *  this function is thread safe!
 */
static bufferslab_t *alloc_slab(stm_tx_t *tx)
{
    DPRINTF("\t\talloc slab: %p\n", tx);
    /* allocate memory and fit slabs into cachelines */
    if (tx->freeslabs==NULL) {
	bufferslab_t *newslabs;
	mem_block_t *alloc;
	int ret = posix_memalign((void**)&newslabs, 64, NRSLABSPERALLOC * SIZEOFSLAB);
	if (newslabs==NULL || ret!=0) {
	    perror("malloc: no free memory!");
	    exit(1);
	}
	if ((alloc = (mem_block_t*)malloc(sizeof(mem_block_t)))==NULL) {
	    perror("malloc: no free memory!");
	    exit(1);
	}
	// save the address for later free
	alloc->addr = newslabs;
	alloc->next = tx->buffers;
	tx->buffers = alloc;
	/* build linked list */
	int i;
	for (i=0; i<(NRSLABSPERALLOC-1); i++) {
	    newslabs[i].next=&(newslabs[i+1]);
	}
	newslabs[(NRSLABSPERALLOC-1)].next=NULL;

	/* enqueue in our tx-local freelist */
	tx->freeslabs = newslabs;
    }
    bufferslab_t *myslab = tx->freeslabs;
    tx->freeslabs = tx->freeslabs->next;

    myslab->size=0;
    myslab->next=NULL;
    return myslab;
}

/* adds a chain of slabs to the freelist */
static void free_slabs(stm_tx_t *tx, bufferslab_t *free, bufferslab_t *last)
{
    last->next = tx->freeslabs;
    tx->freeslabs = free;
}

/*******************************************************************\
 *  INIT and EXIT                                                  *
\*******************************************************************/

/* Called once (from main) to initialize STM infrastructure. */
void stm_init()
{
    DEBUG_START
    DPRINTF("stm init\n");
    GLOBAL_VERSION=1;
    
    allocated = NULL;
    unused_tx = NULL;
    GLOBAL_VERSION=1;
    
#ifdef VALGRIND
    // valgrind cannot memalgin >1meg
    if (posix_memalign((void**)&locks, LOCK_HASH_ARRAY_SIZE/4, LOCK_HASH_ARRAY_SIZE*sizeof(stm_word_t))) {
#else
    if (posix_memalign((void**)&locks, LOCK_HASH_ARRAY_SIZE*sizeof(stm_word_t), LOCK_HASH_ARRAY_SIZE*sizeof(stm_word_t))) {
#endif
	printf("Could not allocate locks\n");
	exit(1);
    }
    lock_reset();
    pthread_mutex_init(&unused_tx_mutex, NULL);

}


/* Called once (from main) to clean up STM infrastructure. */
void stm_exit()
{
    DPRINTF("stm exit\n");
    /* free buffers */
    while (allocated!=NULL) {
	mem_block_t *cur = allocated;
	allocated = allocated->next;
	free(cur->addr);
	free(cur);
    }
    free((stm_word_t*)locks);

    pthread_mutex_destroy(&unused_tx_mutex);
    while (unused_tx!=NULL) {
	tx_block_t *cur = unused_tx;
	unused_tx = unused_tx->next;
	free_tx((stm_tx_t*)cur->tx);
	free(cur);
    }
    
#ifdef GLOBAL_STATS
    printf("Total nr of new transactions: %ld\n", xxstm_nr_tx);
#endif
    DEBUG_END
}

/**
 * Called by the CURRENT thread to obtain an environment for setjmp/longjmp.
 *
 * @param tx is a pointer to the transaction descriptor
 */
jmp_buf *stm_get_env(stm_tx_t *tx)
{
    return &tx->env;
}

/*******************************************************************\
 *  NEW and DELETE                                                 *
\*******************************************************************/

/* called by the current thread to initialize a transaction descriptor */
stm_tx_t *stm_new()
{
    stm_tx_t *newtx;
    if (unused_tx!=NULL) {
	pthread_mutex_lock(&unused_tx_mutex);
	if (unused_tx!=NULL) {
	    tx_block_t *cur = unused_tx;
	    unused_tx = unused_tx->next;
	    newtx = (stm_tx_t*)cur->tx;
	    free(cur);
	    pthread_mutex_unlock(&unused_tx_mutex);
	    return newtx;
	}
	pthread_mutex_unlock(&unused_tx_mutex);
    }
    if ((newtx = (stm_tx_t*)malloc(sizeof(stm_tx_t)))==NULL) {
	perror("malloc: no free memory!");
	exit(1);
    }
    DPRINTF("stm new: %p\n", newtx);
    
#ifdef DEBUG
    memset(newtx, 0x0, sizeof(stm_tx_t));
#endif
    
    int ret = posix_memalign((void**)&(newtx->writehash), 64, sizeof(writeset_t*)*WBUF_MAX_HASH_ARRAY_SIZE);
    newtx->whashsize = WBUF_HASH_ARRAY_SIZE;
    newtx->whashmask = WBUF_HASH_ARRAY_SIZE-1;
    if (newtx->writehash==NULL || ret!=0) {
	perror("malloc: no free memory!");
	exit(1);
    }


    newtx->status = TX_IDLE;

    newtx->freeslabs = NULL;
    newtx->buffers = NULL;
    newtx->writeset = alloc_slab(newtx);

    ret = posix_memalign((void**)&(newtx->lockset), 64, NRRLENTRIESINSET*sizeof(lockset_t));
    ret = ret + posix_memalign((void**)&newtx->readset, 64, 4*NRRLENTRIESINSET*sizeof(readset_t));
    newtx->maxlocks = NRRLENTRIESINSET;
    newtx->locksize = NRRLENTRIESINSET*sizeof(lockset_t);
    newtx->maxreads = 4*NRRLENTRIESINSET;
    newtx->readsize = 4*NRRLENTRIESINSET*sizeof(readset_t);
    if (newtx->readset==NULL || newtx->lockset==NULL || ret!=0) {
	perror("malloc: no free memory!");
	exit(1);
    }

#ifdef ADAPTIVENESS
    // adaptiveness
    newtx->writethrough=1;
#ifdef ADAPTIVEHASH
    newtx->adaptive_hash=0;
#endif
#ifdef ADAPTIVEWHASH2
    newtx->adaptive_hash=8;
#endif
    newtx->wtotal=0;
    newtx->nrtx=0;
#endif
    
    /* clear read and writeset */
    newtx->nr_uniq_writes = 0;
    
#ifdef GLOBAL_STATS
    newtx->retries=0;
    newtx->aborts=0;
    newtx->commits=0;
    xxstm_nr_tx++;
#endif
#ifdef STATS
    newtx->nb_reads = 0;
    newtx->nb_writes = 0;
    newtx->nb_locks=0;
    newtx->nb_min_reads=-1;
    newtx->nb_max_reads=0;
    newtx->nb_min_writes=-1;
    newtx->nb_max_writes=0;
    newtx->nb_tot_reads=0;
    newtx->nb_tot_writes=0;
    newtx->nb_read_ver_err=0;
    newtx->nb_read_ver_err_rec=0;
    newtx->nb_read_ver_change=0;
    newtx->nb_lock_ver_err=0;
    newtx->nb_lock_ver_err_rec=0;
#endif
    
    return newtx;
}


void stm_delete(stm_tx_t *tx)
{
    DPRINTF("stm delete: %p\n", tx);
    /* Check status */
#ifdef GLOBAL_STATS
    printf("Global statistics:\n");
    printf("Nr. commits: %ld\n", tx->commits);
    printf("Nr. retries: %ld\n", tx->retries);
    printf("Nr. aborts: %ld\n", tx->aborts);
#endif
#ifdef STATS    
    unsigned long nrtx=tx->commits+tx->retries;
    nrtx = (nrtx==0) ? 1 : nrtx;
    printf("Avg. nr. locks/tx: %ld\n", tx->nb_locks/nrtx);
    printf("Avg. nr. reads/tx: %ld (min: %ld max: %ld)\n", tx->nb_tot_reads/nrtx, tx->nb_min_reads, tx->nb_max_reads);
    printf("Avg. nr. writes/tx: %ld (min: %ld max: %ld)\n",  tx->nb_tot_writes/nrtx, tx->nb_min_writes, tx->nb_max_writes);
    printf("Nr. of read version failures: %ld (recovered: %ld)\n",  tx->nb_read_ver_err, tx->nb_read_ver_err_rec);
    printf("Nr. of read version changes before return: %ld\n", tx->nb_read_ver_change);
    printf("Nr. of lock version failures: %ld (recovered: %ld)\n",  tx->nb_lock_ver_err, tx->nb_lock_ver_err_rec);
#endif
    assert(tx->status != TX_ACTIVE && tx->status != TX_WAITING);
    
    pthread_mutex_lock(&unused_tx_mutex);
    tx_block_t *cur = (tx_block_t*)malloc(sizeof(tx_block_t));
    cur->next = unused_tx;
    cur->tx = tx;
    unused_tx = cur;
    pthread_mutex_unlock(&unused_tx_mutex);
}

static void free_tx(stm_tx_t *tx)
{
    assert(tx->status != TX_ACTIVE && tx->status != TX_WAITING);
    
    /* free buffers */
    while (tx->buffers!=NULL) {
	mem_block_t *cur = tx->buffers;
	tx->buffers = tx->buffers->next;
	free(cur->addr);
	free(cur);
    }

    free(tx->readset);
    free(tx->lockset);

    free(tx->writehash);
    free(tx);
}

/*******************************************************************\
 * START, COMMIT and ABORT
\*******************************************************************/

/**
 * Start a transaction
 *
 * @param tx is a pointer to the transaction descriptor
 * @param env is a pointer to the jump buffer which is used to return to this point in case of a retry
 *            or NULL to create a jump buffer internally using the stack saving mechanism
 */
void stm_start(stm_tx_t *tx, jmp_buf *env)
{
    DPRINTF("\tstm start: %p\n", tx);
    /* Check status */
    assert(tx->status != TX_ACTIVE && tx->status != TX_WAITING);
    
    //tx->jmp = env;
    
    /* Reset the tansactional memory buffer */
    tx->freed = NULL;
    tx->allocated = NULL;

#ifdef ADAPTIVENESS
    // used for adaptiveness
    tx->wtotal+=tx->nr_uniq_writes;
    tx->nrtx++;
    
    // adapt every 100 transactions
    if (unlikely(tx->adaptcommits%64==0)) {

	// adapt write through or write back
	if ((tx->adaptretries*100) / (tx->adaptcommits+1) > 60) {
	    //if (tx->writethrough==1) printf("changing wt to wb\n");
	    tx->writethrough=0; /* false */
	} else {
	    //if (tx->writethrough==0) printf("changing wb to wt\n");
	    tx->writethrough=1;
	}

#ifdef ADAPTIVE_WHASH
	if ((tx->wtotal/tx->nrtx)*3>tx->whashsize && tx->whashsize<WBUF_MAX_HASH_ARRAY_SIZE) {
	    // more than 33% load in the whashtable -> double the whashtable
	    tx->whashsize*=2;
	    tx->whashmask = tx->whashsize-1;
	    //printf("new whashsize: %d\n", tx->whashsize);
	    //free(tx->writehash);
	    //if (posix_memalign((void**)&(tx->writehash), 64, tx->whashsize*sizeof(writeset_t*))!=0) {
	    //perror("malloc: no free memory!");
	    //exit(1);
	    //}
	}
	if ((tx->wtotal/tx->nrtx)*10<tx->whashsize && tx->whashsize>16) {
	    // less than 10% load in the whashtable -> half the whashtable
	    tx->whashsize/=2;
	    tx->whashmask = tx->whashsize-1;
	    //printf("new whashsize: %d\n", tx->whashsize);
	    //free(tx->writehash);
	    //if (posix_memalign((void**)&(tx->writehash), 64, tx->whashsize*sizeof(writeset_t*))!=0) {
	    //perror("malloc: no free memory!");
	    //exit(1);
	    //}
	}
#endif
	
	if (tx->whashcollisions*10/(tx->wtotal+1)) {
	    // > 25% collisionrate
	    //printf("switching hash fct: %d\n", tx->adaptive_hash);
#ifdef ADAPTIVEHASH
	    tx->adaptive_hash = (tx->adaptive_hash+1) % 6;
#endif
#ifdef ADAPTIVEWHASH2
	    tx->adaptive_hash = (tx->adaptive_hash+3) % 7 + 2;
#endif
	}
	
	tx->whashcollisions=0;
	// reset
	tx->adaptretries = 0;
	tx->adaptcommits = 1;

    }
    tx->writebloom = 0;
#else
    // no adaptiveness: set the writehash to zero and remove wbloom
    sse2_memzero128aligned(tx->writehash, tx->whashsize*sizeof(writeset_t*));
    tx->writebloom = -1;
#endif
    
    /* clear read and writeset */
    tx->nr_uniq_writes = 0;
    tx->nrreads = 0;
    tx->nrlocks = 0;
    
    tx->waiting_for = NULL;
    
#ifdef STATS
    tx->nb_reads = 0;
    tx->nb_writes = 0;
#endif
    /* remember the current version */
    tx->max_version = GLOBAL_VERSION;
#ifdef GLOBAL_STATS
    tx->start    = tx->max_version;
#endif

    /* Change transaction status to TX_ACTIVE*/
    tx->status = TX_ACTIVE;
}

/**
 * Commit this transaction
 *
 * @param tx is a pointer to the transaction descriptor
 */
void stm_commit(stm_tx_t *tx)
{
    stm_word_t commit_version;
    
    DPRINTF("\tstm commit start: %p\n", tx);

    /* Check status */
    assert(tx->status == TX_ACTIVE);
    
    if (tx->nr_uniq_writes==0) {
	/* No need to acquire, validate or extend anything in read only mode */
	tx->status = TX_COMMITTED;
    } else {
	/* Try to acquire all locks */
	buf_acquire_all_locks(tx);
	
	/* Increment the counter and get the newest version */
	commit_version = GLOBAL_VERSION_INC+2;
	
	/* Special case: if max_version + 2 == commit_version we do not need to validate */
	if ((tx->max_version)+2 != commit_version) {
	    /* Before we can write back we need to validate the read set */
	    if (unlikely(!buf_validate(tx))) {
		/* This is the end of this function since stm_retry never returns */
		DPRINTF("\tstm commit validate failed: %p\n", tx);
		stm_retry(tx);
	    }
	}
	/* The locks are acquired and the read set validated */
	tx->status = TX_COMMITTED;
	
	/* Write the write buffer back to the shared memory */
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
	if (!tx->writethrough)
	    buf_write_back(tx);
#elif defined(WRITEBACK)
	buf_write_back(tx);
#endif

	buf_release_all_locks(tx, commit_version);
    }
    
    /* Free the memory which was freed during the transaction */
    mem_free_memory(tx);
    
    /* Reset the buffer */
    buf_reset(tx);

    DPRINTF("\tstm commit done: %p\n", tx);

#ifdef ADAPTIVENESS
    tx->adaptcommits++;
#endif

#ifdef GLOBAL_STATS
    tx->commits++;
#endif
#ifdef STATS
    tx->nb_tot_reads+=tx->nb_reads;
    tx->nb_tot_writes+=tx->nb_writes;
    if (tx->nb_writes>tx->nb_max_writes) tx->nb_max_writes = tx->nb_writes;
    if (tx->nb_writes<tx->nb_min_writes) tx->nb_min_writes = tx->nb_writes;
    if (tx->nb_reads>tx->nb_max_reads) tx->nb_max_reads = tx->nb_reads;
    if (tx->nb_reads<tx->nb_min_reads) tx->nb_min_reads = tx->nb_reads;
#endif
    /* Check status */
    assert(tx->status == TX_COMMITTED);
}

static inline __always_inline void stm_abort_or_retry_helper(stm_tx_t *tx) {
    
    /* if we are in a writethrough mode we first need to undo all changes! */
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
    if (tx->writethrough)
	buf_write_back(tx);
#elif defined(WRITETHROUGH)
    buf_write_back(tx);
#endif

    buf_release_all_locks(tx, 0);

    tx->status = TX_ABORTED;

    /* Deallocat allocated memory during transaction */
    mem_free_memory(tx);
    
    /* reset the rw_buffer */
    buf_reset(tx);
}

/**
 * Retry this transaction
 *
 * @param tx is a pointer to the transaction descriptor
 */
inline __always_inline void stm_retry(stm_tx_t *tx)
{
    DPRINTF("\tstm retry: %p\n", tx);

    /* Check status */
    assert(tx->status == TX_ACTIVE || tx->status == TX_WAITING);
    
    stm_abort_or_retry_helper(tx);
    
    tx->adaptretries++;

#ifdef GLOBAL_STATS
    tx->retries++;
#endif
    
    //if (tx->jmp != NULL) {
    longjmp(tx->env,1);
    //} else {
    //perror("no longjmp destination\n");
    //exit(1);
    //}
}


/*******************************************************************\
 *  LOAD and STORE                                                 *
\*******************************************************************/

/**
 * Called by the CURRENT thread to load a word-sized value.
 *
 * @param tx is a pointer to the transaction descriptor
 * @param addr is the address to load
 */
stm_word_t stm_load(stm_tx_t *tx, stm_word_t *addr)
{
    DPRINTF("\t\tstm load: %p (%p)", tx, addr);

#ifdef STATS
    tx->nb_reads++;
#endif
    /* Check status */
    assert(tx->status == TX_ACTIVE);

    /* make sure that we read the correct version */
    return buf_check_read(tx, addr);
}


/**
 * Called by the CURRENT thread to store a word-sized value.
 *
 * @param tx is a pointer to the transaction descriptor
 * @param addr is the address to write to
 * @param value is the value to write to addr
 */
void stm_store(stm_tx_t *tx, stm_word_t *addr, stm_word_t value)
{
    DPRINTF("\t\tstm write: %p (%p=%p)\n", tx, addr, (void*)value);
    writeset_t *write;
#ifdef STATS
    tx->nb_writes++;
#endif
    write = buf_get_write_addr(tx, addr, 1, value);
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
    if (tx->writethrough) {
	*addr = value;
    } else {
	write->value = value;
    }
#elif defined(WRITEBACK)
    write->value = value;
#elif defined(WRITETHROUGH)
    *addr = value;
#else
    assert(1=0); // either WRITEBACK, or WRITEHTROUGH or ADAPTIVENESS and WRITEBACK and WRITETHROUGH must be defined!
#endif
}



/*******************************************************************\
 * Functions to work with the read buffers and write buffers
\*******************************************************************/

static inline void buf_acquire_all_locks(stm_tx_t *tx)
{
#ifndef EAGER_LOCKING
    /* aquire all write locks */
    bufferslab_t *wset = tx->writeset;
    stm_word_t i;
    while (wset!=NULL) {
	for (i=0; i<wset->size; i++) {
	    /* acquire this lock and save the version */
	    lock_acquire(tx, wset->data.writes[i].addr);
	}
	wset = wset->next;
    }
#endif
}

static inline __always_inline void buf_release_all_locks(stm_tx_t *tx, stm_word_t version)
{
    /* release all write locks */
    lockset_t *mylocks = tx->lockset;
    stm_word_t i;
    if (version==0) {
	for (i=0; i<tx->nrlocks; i++) {
	    stm_word_t *lockaddr = mylocks[i].lock;
	    /* release the lock and save the version */
	    assert(*lockaddr==(stm_word_t)tx);
	    LOCK_RELEASE(lockaddr, mylocks[i].version);
	}
    } else {
	for (i=0; i<tx->nrlocks; i++) {
	    stm_word_t *lockaddr = mylocks[i].lock;
	    /* release the lock and save the version */
	    assert(*lockaddr==(stm_word_t)tx);
	    LOCK_RELEASE(lockaddr, version);
	}
    }
}

/**
 * This is a special version of buf_validate that forwards one lock
 * that has been taken speculatively and we are currently trying to
 * validate that specific lock and to extend the readset version
 * So we have to check that lock as well!
 */
static inline stm_word_t buf_validate_lockspecial(stm_tx_t *tx, stm_word_t xlockValue, stm_word_t *xlockaddr)
{
    readset_t *rset = tx->readset;
    stm_word_t *lockaddr, lockValue, i;

    /* Check the status */
    assert(tx->status == TX_ACTIVE);

    for (i=0; i<tx->nrreads; i++) {
	readset_t *thisread = &(rset[i]);
	lockaddr = thisread->lock;
	assert(lockaddr!=NULL);	    
	lockValue = *lockaddr;
	if (lockaddr==xlockaddr) lockValue = xlockValue; // forward value
#ifdef EAGER_LOCKING
	/* Check if the lock value has changed since we first read it */
	if ((LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx) &&
	    (thisread->version!=LOCK_GET_VERSION_FROM_VALUE(lockValue)))
#else
	/* Check if the lock value has changed since we first read it */
	/* or check the version stored in the write buffer if this transaction owns the lock*/
	/*    TODO: buf_get_write_lock_version checks lock value from earlier write, that has
		not yet been acquired. need to change this!
	if ((LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx)
	    ? (thisread->version != LOCK_GET_VERSION_FROM_VALUE(lockValue))
	    : (thisread->version != buf_get_write_lock_version(tx, lockaddr)))
	*/
	assert(LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx);
	if (thisread->version != LOCK_GET_VERSION_FROM_VALUE(lockValue))
#endif
	{
	    DPRINTF("special validate: wrong version: %lx != %lx (tx: %p)\n", thisread->version, lockValue, tx);
	    return 0;
	}
    }
    return 1;
}
static inline stm_word_t buf_validate(stm_tx_t *tx)
{
    readset_t *rset = tx->readset;
    stm_word_t *lockaddr, lockValue, i;

    /* Check the status */
    assert(tx->status == TX_ACTIVE);

    for (i=0; i<tx->nrreads; i++) {
	readset_t *thisread = &(rset[i]);
	lockaddr = thisread->lock;
	assert(lockaddr!=NULL);	    
	lockValue = *lockaddr;
#ifdef EAGER_LOCKING
	/* Check if the lock value has changed since we first read it */
	if ((LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx) &&
	    (thisread->version!=LOCK_GET_VERSION_FROM_VALUE(lockValue)))
#else
	/* Check if the lock value has changed since we first read it */
	/* or check the version stored in the write buffer if this transaction owns the lock*/
	/*    TODO: buf_get_write_lock_version checks lock value from earlier write, that has
		not yet been acquired. need to change this!
	if ((LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx)
	    ? (thisread->version != LOCK_GET_VERSION_FROM_VALUE(lockValue))
	    : (thisread->version != buf_get_write_lock_version(tx, lockaddr)))
	*/
	assert(LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx);
	if (thisread->version != LOCK_GET_VERSION_FROM_VALUE(lockValue))
#endif
	{
	    DPRINTF("validate: wrong version: %lx != %lx (tx: %p)\n", thisread->version, lockValue, tx);
	    return 0;
	}
    }
    return 1;
}

/**
 * Write back all data from our local (write) buffer into memory
 * We don't worry about locks, this function assumes that all locks
 * are already taken!
 * (The locks will be freed later by release_all_locks!)
 */
static inline void buf_write_back(stm_tx_t *tx)
{
    /* Check the status */
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
    assert((tx->status == TX_COMMITTED && !tx->writethrough) || (tx->status == TX_ABORTED && tx->writethrough) );
#elif defined(WRITEBACK)
    assert(tx->status == TX_COMMITTED);
#elif defined(WRITETHROUGH)
    assert(tx->status == TX_ABORTED);
#endif    
    
    /* write back */
    bufferslab_t *wset = tx->writeset;
    volatile stm_word_t *addr;
    stm_word_t i;
    while (wset!=NULL) {
#ifndef NO_SSE
	// prefetch wset->next does not help!
	//__builtin_prefetch(wset->next);
#endif
	for (i=0; i<wset->size; i++) {
	    addr = wset->data.writes[i].addr;
	    /* write back */
	    *addr = wset->data.writes[i].value;
	}
	wset = wset->next;
    }
}

static inline __always_inline stm_word_t wbuf_idx_from_addr(stm_tx_t *tx, stm_word_t *addr)
{
#if defined(ADAPTIVENESS) && (defined(ADAPTIVEHASH) || defined(ADAPTIVEWHASH2))
    switch (tx->adaptive_hash) {
    case 0: return (((stm_word_t)addr >> 8) & WBUF_MASK);
    case 1: return (((stm_word_t)addr >> 6) & WBUF_MASK);
    case 2: return (((stm_word_t)addr >> 4) & WBUF_MASK);
    case 3: return (((stm_word_t)addr >> 2) & WBUF_MASK);
    case 4: return ((((stm_word_t)addr >> 16)^((stm_word_t)addr>>5)) & WBUF_MASK);
    case 5: return ((((stm_word_t)addr >> 12)^((stm_word_t)addr>>2)) & WBUF_MASK);
    default:
	return WBUF_IDX_FROM_ADDR(tx, addr);
    }
#else
    return WBUF_IDX_FROM_ADDR(tx, addr);
#endif
}

static inline __always_inline writeset_t *buf_get_write_addr(stm_tx_t *tx, stm_word_t *addr, stm_word_t allocate, stm_word_t value)
{
    writeset_t *hashptr, **hashentry;
    
    /* Check status */
    assert(tx->status == TX_ACTIVE);

#ifdef ADAPTIVENESS
    // we don't need a hash table yet, there are only few writes!
    if (likely(tx->nr_uniq_writes<=NRWBEFOREHASH)) {
	stm_word_t i;
	writeset_t *writes = tx->writeset->data.writes;
	// use switch optimization
	switch (tx->nr_uniq_writes) {
	case 11: if (writes->addr == addr) return writes; writes++;
	case 10: if (writes->addr == addr) return writes; writes++;
	case 9: if (writes->addr == addr) return writes; writes++;
	case 8: if (writes->addr == addr) return writes; writes++;
	case 7: if (writes->addr == addr) return writes; writes++;
	case 6: if (writes->addr == addr) return writes; writes++;
	case 5: if (writes->addr == addr) return writes; writes++;
	case 4: if (writes->addr == addr) return writes; writes++;
	case 3: if (writes->addr == addr) return writes; writes++;
	case 2: if (writes->addr == addr) return writes; writes++;
	case 1: if (writes->addr == addr) return writes; writes++;
	case 0: break;
	default:
	    for (i=0; i<tx->nr_uniq_writes; i++) {
		if (writes->addr == addr) return writes;
		writes++;
	    }
	}
	if (allocate) {
	    /* make sure, that we have the lock as well */
#ifdef EAGER_LOCKING
	    lock_acquire(tx, addr);
	    asm __volatile__("": : :"memory");
#endif
	    writes->addr=addr;
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
	    if (tx->writethrough) {
		writes->value = *addr;
	    }
#elif defined(WRITETHROUGH)
	    writes->value = *addr;
#endif
#ifdef WRITEBLOOM
	    tx->writebloom|=WBLOOMHASH((stm_word_t)addr);
#endif
	    tx->writeset->size = ++tx->nr_uniq_writes;
	    if (tx->nr_uniq_writes<=NRWBEFOREHASH) {
		return writes;
	    } else {
		// build up hash list and enqueue existing entries
		sse2_memzero128aligned(tx->writehash, tx->whashsize*sizeof(writeset_t*));
		writes = tx->writeset->data.writes;
		// enqueue existing entries
		for (i=0; i<tx->nr_uniq_writes; i++) {
		    hashentry = ADDR2WENTRY(tx, tx->writehash, writes[i].addr);
		    //tx->writebloom|=WBLOOMHASH((stm_word_t)writes[i].addr);
		    writes[i].next = (*hashentry);
#if defined(ADAPTIVENESS)
		    if (*hashentry!=NULL) tx->whashcollisions++;
#endif
		    *hashentry = &(writes[i]);
		}
		return &(writes[NRWBEFOREHASH]);
	    }
	} else {
	    return NULL;
	}
    }
#endif
    
    hashentry = ADDR2WENTRY(tx, tx->writehash, addr);

    /* if we don't have a hit in the bloom filter we know for sure that
     * this is a new entry and can skip directly to the allocate part
     * otherwise we must check the hashtable for such an entry
     * (this is expensive!)
     */
#ifdef WRITEBLOOM
    //if (((WBLOOMHASH((stm_word_t)addr)|tx->writebloom)^tx->writebloom)) {
    stm_word_t wbloomhash = WBLOOMHASH((stm_word_t)addr);
    if ((wbloomhash & tx->writebloom) != wbloomhash) {
	hashptr=NULL;
    } else {
#endif
	hashptr = *hashentry;
	
	/* fast forward to the correct entry */
	while (hashptr!=NULL && hashptr->addr!=addr) {
	    hashptr = hashptr->next;
	}
#ifdef WRITEBLOOM
    } 
#endif
    
    /* return the address if we found the entry */
    /* it is either NULL or contains our hashptr with the correct addr */
    if (hashptr!=NULL) {
#ifndef EAGER_LOCKING
    	// if not eager locking -> check if addr still valid!
	stm_word_t version = lock_safe_get_value(tx, ADDR2LOCKADDR(addr));
	if (version>tx->max_version) {
	    DPRINTF("write: abort: version>max_version\n");
	    stm_retry(tx);
	}
#endif
	return hashptr;
    }
    
    /* maybe we need to allocate a new one */
    if (allocate) {
	/* make sure, that we have the lock as well */
#ifdef EAGER_LOCKING
	lock_acquire(tx, addr);
	asm __volatile__("": : :"memory");
#endif
	++tx->nr_uniq_writes;
	// no more space - need to allocate new slab
	if (unlikely(tx->writeset->size==NRWRITESINSLAB)) {
	    bufferslab_t *slab = alloc_slab(tx);
	    slab->next = tx->writeset;
	    tx->writeset = slab;
	}

	writeset_t *newwrite = &(tx->writeset->data.writes[tx->writeset->size++]);
#if defined(ADAPTIVENESS)
	if (*hashentry!=NULL) tx->whashcollisions++;
#endif
	newwrite->next = (*hashentry);
	newwrite->addr = addr;
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
	if (tx->writethrough) {
	    newwrite->value = *addr;
	}
#elif defined(WRITETHROUGH)
	newwrite->value = *addr;
#endif
#ifdef WRITEBLOOM
	tx->writebloom|=WBLOOMHASH((stm_word_t)addr);
#endif
	*hashentry = newwrite;

	return newwrite;
    }
    return NULL;
}

/**
 * Reads a memory location transactionally
 * There are several cases we must consider
 * * we already have the lock on that memory region
 *   * we already wrote to that memory address -> return the (cached) value
 *   * we did not write to that memory address -> return *addr
 * * we did not write to that memory region
 *   * check if we already read from that location and check version -> return *addr
 *     (this might not be needed)
 *   * add a new read entry and check the version -> return *addr
 */
static inline __always_inline stm_word_t buf_check_read(stm_tx_t *tx, stm_word_t *addr)
{
    volatile stm_word_t *lock;
    readset_t *hashptr;
    stm_word_t value, version;
    
    /* Check status */
    assert(tx->status == TX_ACTIVE);

    /* get the lock */
    lock = ADDR2LOCKADDR(addr);

#if defined(EAGER_LOCKING)
    if (LOCK_GET_OWNER_ADDR_FROM_VALUE(*lock)==tx) {
	// we own the lock! (e.g. we wrote to a different address that is
	// covered by the same lock or wrote to this addr)

	
	// did we already write to this address?
#if defined(ADAPTIVENESS) && defined(WRITEBACK) && defined(WRITETHROUGH)
	writeset_t *write;
#ifdef WRITEBLOOM
	if (!tx->writethrough && !((WBLOOMHASH((stm_word_t)addr)|tx->writebloom)^tx->writebloom) && (write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#else
	if (!tx->writethrough && (write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#endif
	    DPRINTF(" (in wset, tx: %p lock %p: %p val:%p)\n", tx, lock, (void*)tx->max_version, (void*)write->value);
	    return write->value;
	}
#elif defined(WRITEBACK)
	writeset_t *write;
#ifdef WRITEBLOOM
	if (!((WBLOOMHASH((stm_word_t)addr)|tx->writebloom)^tx->writebloom) && (write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#else
	if ((write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#endif
	    DPRINTF(" (in wset, tx: %p lock %p: %p val:%p)\n", tx, lock, (void*)tx->max_version, (void*)write->value);
	    return write->value;
	}
#endif
	DPRINTF(" (in wset, tx: %p lock %p: %p val:%p)\n", tx, lock, (void*)tx->max_version, (void*)*addr);
	// therefore we just return the value!
	// do we need to check if hashptr is NULL?
	// no! buf_validate does this (or we validated when we took the
	// lock in the write case!)	
	return *addr;
    }
#else
    // lazy locking - we need to check if we already read from that place
    writeset_t *write;
#ifdef WRITEBLOOM
    if (!((WBLOOMHASH((stm_word_t)addr)|tx->writebloom)^tx->writebloom) && (write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#else
    if ((write=buf_get_write_addr(tx, addr, 0, 0))!=NULL) {
#endif
	return write->value;
    }
#endif

 buf_check_read_retry:
    /* get the version of the lock (or tx that holds the lock) */
    /* other path: we have to check the lock (but need the safe version) */
#ifdef SAFE_MODE
    do {
#endif
	/* Get lock */
	version = lock_safe_get_value(tx, lock);
#ifdef SAFE_MODE
	/* try to acquire lock */
	/* the safe mode acquires the lock during the read section, but we do
	   not account for this lock in any way! */
    } while (!LOCK_SET_OWNER_ADDR(lock, version, (stm_word_t)tx));
#endif
    DPRINTF(" (tx: %p lock %p: %p/%p val:%p)\n", tx, lock, (void*)version, (void*)tx->max_version, (void*)*addr);

    // the second part of this assertion does not hold
    // maybe we already wrote to an address that is covered by the same lock (hash collission)
    //assert(hashptr==NULL || LOCK_GET_OWNER_ADDR_FROM_VALUE(LOCK_GET_VALUE(lock))!=tx);

    /* the lock has an incorrect version
     * we must check if we can extend the readset (if we didn't yet read
     * from that location), otherwise we must abort
     */
    if (unlikely(version > tx->max_version)) {
	stm_word_t current = GLOBAL_VERSION;
	/* are all the older reads still valid? */
#ifdef SAFE_MODE
	// give up lock if we might retry (this lock is not accounted for)
	*lock = version;
#endif
	/* if we already read from that location OR we cannot validate our buffer,
	 * then we retry
	 */
	// we can recover in ~50% of the cases, seems a fair deal!
#ifdef STATS
	tx->nb_read_ver_err++;
#endif
	if (!buf_validate(tx)) {
	    /* This is the end of this function since stm_retrynever returns */
	    DPRINTF("read: abort: version>max_version\n");
	    stm_retry(tx);
	}
#ifdef STATS
	tx->nb_read_ver_err_rec++;
#endif
#ifdef SAFE_MODE
	do {
	    /* Get lock */
	    version = lock_safe_get_value(tx, lock);
	    /* try to acquire lock */
	} while (!LOCK_SET_OWNER_ADDR(lock, version, (stm_word_t)tx));

	// we are in safe mode _and_ hold the lock, but the read set extension failed
	// therefore we abort
	if (version>tx->max_version) {
	    *lock=version;
	    stm_retry(tx);
	}
#endif
	// yes, we can extend the version!
	tx->max_version = current;
    }
    
    /* if we are not in the safe mode, then this read could fail! */
    /* it could be that another thread freed our memory location after
       we checked the version above */
    asm __volatile__("": : :"memory");
    value = *addr;
    asm __volatile__("": : :"memory");
    
#ifndef SAFE_MODE
    // check if the version is still the same (needed for correctness).
    // if we use WT then another w-tx might already have written this location!
    if (unlikely(version != *lock)) {
#ifdef STATS
	tx->nb_read_ver_change++;
#endif
	// let's not abort but recheck this read (goto is nicer than a loop)
	goto buf_check_read_retry;
	//stm_retry(tx);
    }
#endif

    /* allocate a new read entry (list might contain duplicates, but enqueuing is faster than checking */
    if (unlikely(tx->nrreads==tx->maxreads)) {
	tx->readsize *= 2;
	tx->maxreads=tx->readsize/sizeof(readset_t);
	DPRINTF("read larger: %ld (%ld) %p\n", tx->maxreads, tx->readsize, tx);
	//if (posix_memalign((void**)&new, 64, tx->readsize)!=0) { abort(); }
	// TODO: optimize the memcpy!
	//memcpy(new, tx->readset, tx->readsize/2);
	if ((tx->readset = (readset_t*)realloc(tx->readset, tx->readsize))==0) { printf("no mem\n"); abort(); }
	
	//free(tx->readset);
	//tx->readset=new;
    }
    hashptr = &(tx->readset[tx->nrreads++]);
    hashptr->lock = (stm_word_t*)lock;

    hashptr->version = version;

#ifdef SAFE_MODE
    // we are in SAFE_MODE - return read lock
    *lock = version;
#endif

    return value;
}

/**
 * Resets the read write buffer
 * - all blocks are reset and stored in a
 *   recycle list.
 *
 * * @param tx is a pointer to the transaction descriptor
 */
static inline void buf_reset(stm_tx_t *tx)
{
    bufferslab_t *slabs, *last;

    slabs = tx->writeset->next;
    tx->writeset->size=0;
    tx->writeset->next=NULL;
    if (slabs!=NULL) {
	last = slabs;
	while (last->next!=NULL) {
	    last = last->next;
	}
	free_slabs(tx, slabs, last);
    }

}


/*******************************************************************\
 * Locking functions
\*******************************************************************/

static inline __always_inline stm_word_t lock_safe_get_value(stm_tx_t *tx, volatile stm_word_t *lock)
{
	stm_word_t lockValue;

	/* Ensure that the lock is free */
	lockValue = *lock;

	/* common case: lock is free or we have the lock */
	/* if this is not the case, then we try to handle the conflict */
	if (LOCK_IS_FREE(lockValue) || (LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue)==tx)) {
	    return lockValue;
	} else {
	    while (!LOCK_IS_FREE(lockValue) && LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue) != tx) {
		cont_handle_conflict(tx, LOCK_GET_OWNER_ADDR_FROM_VALUE(lockValue));
		lockValue = *lock;
	    }
	    /* If there was a conflict the the status was set to TX_WAITING */
	    tx->status = TX_ACTIVE;
	}
	return lockValue;
}

static inline __always_inline void lock_acquire(stm_tx_t *tx, stm_word_t *addr)
{
    stm_word_t lockValue;
#ifdef STATS
    tx->nb_locks++;
#endif
    volatile stm_word_t *lockaddr = ADDR2LOCKADDR(addr);

    // do we already own the lock?
    DPRINTF("lockaddr: %p (value: %p, tx %p)\n", lockaddr, (void*)*lockaddr, tx);
    if (LOCK_GET_OWNER_ADDR_FROM_VALUE(*lockaddr)==tx) { return; }
    // no, then let's get it!
    do {
	/* Get lock */
	lockValue = lock_safe_get_value(tx, lockaddr);
	/* try to acquire lock */
    } while (!LOCK_SET_OWNER_ADDR(lockaddr, lockValue, (stm_word_t)tx));

    // check that the version of the lock is smaller that our max version
    if (unlikely(lockValue>tx->max_version)) {
	//stm_word_t current = GLOBAL_VERSION;
	/* are all the older reads still valid? */
	/* if we already read from that location OR we cannot validate our buffer,
	 * then we retry
	 */
	// we can recover in ~<<50% of the cases, seems a fair deal!
#ifdef STATS
	//tx->nb_lock_ver_err++;
#endif
	// never works!
	//if (!buf_validate_lockspecial(tx, lockValue, (stm_word_t*)lockaddr)) {
	//DPRINTF("lock is too large %d > %d (tx %p)\n", lockValue, tx->max_version, tx);
	    *lockaddr=lockValue;
	    /* This is the end of this function since stm_retry never returns */
	    stm_retry(tx);
	    //}
#ifdef STATS
	    //tx->nb_lock_ver_err_rec++;
#endif
	// yes, we can extend the version!
	//tx->max_version = current;
	
	//*lockaddr=lockValue;
	//stm_retry(tx);
    }
    assert(lockValue<=tx->max_version);

    // no more space, allocate new slab
    if (unlikely(tx->nrlocks==tx->maxlocks)) {
	lockset_t *new;
	tx->locksize *= 2;
	tx->maxlocks = tx->locksize/sizeof(lockset_t);
	DPRINTF("lock larger: %ld (%ld) %p\n", tx->maxlocks, tx->locksize, tx);
	if (posix_memalign((void**)&new, 64, tx->locksize)!=0) { abort(); }
	// TODO: optimize the memcpy!
	memcpy(new, tx->lockset, tx->locksize/2);
	free(tx->lockset);
	tx->lockset=new;
    }
    
    /* the lock has been acquired */
    // enqueue the lockaddr in our locklist
    assert(lockaddr!=NULL);
    tx->lockset[tx->nrlocks].lock = (stm_word_t*)lockaddr;
    tx->lockset[tx->nrlocks++].version = LOCK_GET_VERSION_FROM_VALUE(lockValue);
    //add_lock_to_lockset(tx, lockaddr, lockValue
}

static void lock_reset()
{
	volatile stm_word_t *curLock = locks;

#ifndef NO_SSE
	int i;

	int n_div_128 = (LOCK_HASH_ARRAY_SIZE*sizeof(stm_word_t)) / 128;

	assert((LOCK_HASH_ARRAY_SIZE*sizeof(stm_word_t)) % 128==0);
	
	// load the first 4 words into the xmm register
#ifdef __LP64__
	__m128i d = (__m128i)_mm_set_epi64((__m64)LOCK_FREE, (__m64)LOCK_FREE);
#else    
	__m128i d = (__m128i)_mm_set_epi32(LOCK_FREE, LOCK_FREE, LOCK_FREE, LOCK_FREE);
#endif
	
	char *p = (char*)curLock;
	for(i = 0; i < n_div_128; i++) {
		_mm_store_si128((__m128i *)&p[0], d);
		_mm_store_si128((__m128i *)&p[16], d);
		_mm_store_si128((__m128i *)&p[32], d);
		_mm_store_si128((__m128i *)&p[48], d);
		_mm_store_si128((__m128i *)&p[64], d);
		_mm_store_si128((__m128i *)&p[80], d);
		_mm_store_si128((__m128i *)&p[96], d);
		_mm_store_si128((__m128i *)&p[112], d);
		p += 128;
	}
	_mm_sfence();
#else
	volatile stm_word_t *endPointer = locks + LOCK_HASH_ARRAY_SIZE;
	while (curLock < endPointer) { *curLock++ = LOCK_FREE; }
#endif
	
}


/*******************************************************************\
 * Conflict handling
\*******************************************************************/

static inline __always_inline void cont_handle_conflict(stm_tx_t *tx, stm_tx_t *other)
{
    stm_tx_t *next;
    
    if(tx->status == TX_ACTIVE) {
	/* The first time that this conflict occures */
	tx->waiting_for = other;
	tx->status = TX_WAITING;
	tx->yielded = 0;
	
	/* Check for dead-lock */
	next = tx;
	
	while ((next = next->waiting_for)) {
	    if (next == tx) {
		DPRINTF("dead lock detected: %p - %p", tx, other);
		stm_retry(tx);
	    }
	    if (next->status != TX_WAITING) {
		break;
	    }
	}
    }

#ifdef EXPDROPOFF
    if (tx->yielded > MAX_NUM_YIELD_PER_LOCK*(tx->adaptretries)) {
#else
    if (tx->yielded > MAX_NUM_YIELD_PER_LOCK) {
#endif
	DPRINTF("yielded %i times - giving up (tx: %p)...\n", tx->yielded, tx);
	// TODO: better contention management
	stm_retry(tx);
    }
    tx->yielded++;
    
    /* Give the other transactions time to finish their work */
    sched_yield();
}


/*******************************************************************\
 * Memory management inside transactions
\*******************************************************************/

/**
 * Enables some precautions that make the fastSTM memory manager more compatible with assumptions made by
 * the yada STAMP benchmark. (Not 100% though)
 * Enabling this unfortunately has a severe performance penalty for other benchmarks, so we disable them for now.
 */
// #define YADA_COMPATIBLE_MEM_MANAGER

/**
 * Gets the size of a block of memory allocated by malloc/realloc.
 * Depends on the implementation details of the memory library
 * (The tested glibc implementation stores the size right before the block (-1).
 *  The 3 (0x7) LSB bits of the stored value is used for other purposes and
 *  an additional 4 (-4) bytes at the end of the block are also used internally)
 */
#define SIZE_OF_ALLOCATED_MEMORY(addr) (((*(((size_t*)(addr))-1)) & (~0x7)) -4)

/**
 * Called by the CURRENT thread to allocate memory within a transaction.
 *
 * @param tx is a pointer to the transaction descriptor
 * @param size is the number of bytes to allocate
 * @return the address of the allocated memory
 */
void *stm_malloc(stm_tx_t *tx, size_t size)
{
    mem_block_t *new_block;
    void *new_addr;
    
    /* Check status */
    assert(tx->status == TX_ACTIVE);
    
    /* Try to allocate the requested memory */
    if((new_addr = (void *)malloc(size)) == NULL) {
	perror("malloc: no free memory!");
	exit(1);
	}
    DPRINTF("\t\tstm malloc: %p (%ld bytes = %p)\n", tx, size, new_addr);
    
    //printf("malloc: %p %d\n", new_addr, size);
#ifdef DEBUG
    memset(new_addr, 0x0, size);
#endif
    
    /* In rare cases it is possible that the current transaction has already written to this new memory block,
       that another parallel transaction has in the meantime freed. Because the current transaction will assume
       this block is private it can write to it directly, and later read from it transactionally.
       This leads to inconsistencies. To avoid these we abort&retry now. */
    /* This could be done faster as one new routine in the rw buffer module
       that just checks if there are any writes to this region */
#ifdef YADA_COMPATIBLE_MEM_MANAGER
    size_t i;
    PRINT_INFO_N(2, "Testing new memory block %p for conflicts:", new_addr);
    for(i = 0; i < size; i += sizeof(stm_word_t)) {
	
	/* This is the simplest (but not very efficient) way to check if there is already a write entry in the buffer */
	void *addr = new_addr + i;
	volatile stm_word_t *lock = GET_LOCK(addr);
	int ret_type;
	buf_read_shared_memory (tx, addr, lock, &ret_type, 0);
	
	if (ret_type == 2) {
	    /* There is already a write entry. This transaction is doomed. */
	    PRINT_INFO_N(2, "Conflict: previous writes to newly allocated memory block %p detected", new_addr);
	    free(new_addr);
	    stm_retry(tx);
	}
    }
#endif
    
    /* Prepare a mem_block to keep a reference to the allocated memory */
    if((new_block = (mem_block_t *)malloc(sizeof(mem_block_t))) == NULL) {
	perror("malloc: no free memory!");
	exit(1);
    }
    //int i;
    //for (i=0; i<size/sizeof(stm_word_t); i++) {
    //  stm_store(tx, ((stm_word_t*)new_addr)+i, 0x0);
    //}
    
    new_block->addr = new_addr;
    
    /* Insert the mem_block into the transaction descriptor */
    new_block->next = tx->allocated;
    tx->allocated = new_block;
    
    /* Return the allocated memory */
    return new_block->addr;
}

/**
 * Called by the CURRENT thread to free memory within a transaction.
 *
 * @param tx is a pointer to the transaction descriptor
 * @param addr is the pointer to the memory which should be freed
 */
//extern void* MallocExtension_GetAllocatedSize(void *ptr);

void stm_free(stm_tx_t *tx, void *addr)
{
    mem_block_t *free_block;
    
	/* Check status */
    assert(tx->status == TX_ACTIVE);
    DPRINTF("\t\tstm free: %p (%p)\n", tx, addr);
    
    /* We need to lock memory in order to prevent others from accessing it. */

    /* we only know, that we have at least 4 bytes, so we lock this to prevent
       other threads from overwriting or accessing these values!
       TODO: this is unsafe, we should 'write'/lock the complete block!
    */
    // TODO need a cleaner way for this!
    int size = SIZE_OF_ALLOCATED_MEMORY(addr)-4;
    //printf("free %p %d\n", addr, size);
    int i;
#ifdef EAGER_LOCKING
    void *lockaddr=NULL;
#endif
    for (i=0; i<size/sizeof(stm_word_t); i++) {
	// just grab the locks and increase the version if we commit
	// a normal store would be the (slow) alternative
#ifdef EAGER_LOCKING
	void *tmp = (void*)ADDR2LOCKADDR((((stm_word_t*)addr)+i));
	if (tmp!=lockaddr) {
	    lock_acquire(tx, ((stm_word_t*)addr)+i);
	    lockaddr=tmp;
	}
#else
	buf_get_write_addr(tx, addr, 1, 0);
#endif
    }
    
    /* Prepare a mem_block to keep a reference to the freed memory */
    if((free_block = (mem_block_t *)malloc(sizeof(mem_block_t))) == NULL) {
	perror("malloc: no free memory!");
	exit(1);
    }
    
    /* Insert the mem_block into the transaction descriptor */
    free_block->addr = addr;
    free_block->next = tx->freed;
    tx->freed = free_block;
    
}

#if 0
/**
 * Called by the CURRENT thread to reallocate memory within a transaction.
 *
 * @param tx is a pointer to the transaction descriptor
 * @param addr is the pointer to the memory which should be reallocated
 * @param size is the number of bytes to allocate
 * @return the address of the allocated memory
 */
void *stm_realloc(stm_tx_t *tx, void *addr, size_t size) {
    size_t old_size;
    void *new_addr, *stm_addr, *stm_new_addr;
    
    /* make sure that this function is not run - locks are not updated for reallocated area */
    assert(size==-1);
    
    /* Check status */
    assert(tx->status == TX_ACTIVE);
    
    /* realloc NULL behaves like malloc */
    if (addr == NULL) {
	new_addr = stm_malloc(tx, size);
    } else {
	
	/* How big was the old b lock? */
	old_size = SIZE_OF_ALLOCATED_MEMORY(addr);
	
	/* If it was big enough, don't bother doing anything */
	if (old_size >= size) {
	    new_addr = addr;
	} else {
	    /* Otherwise get a new bigger block, copy and release the old block */
	    new_addr = stm_malloc(tx, size);
	    stm_addr = (void*)stm_get_read_addr(tx, addr, old_size);
	    stm_new_addr = (void*)stm_get_write_addr(tx, new_addr, old_size);
	    memcpy(stm_new_addr, stm_addr, old_size);
	    stm_finish_writing(tx, new_addr, old_size);
	    stm_free(tx, addr);
	}
    }
    
    /* Return the allocated memory */
    return new_addr;
}
#endif

/**
 * Called by the CURRENT thread upon commit or abort.
 * Depending on the status of the transaction will the
 * allocated memory be freed or the freed memory be released
 *
 * @param tx is a pointer to the transaction descriptor
 */
inline __always_inline void mem_free_memory(stm_tx_t *tx)
{
    mem_block_t *cur, *next, *release, *keep;
    
    /* Decide which memory needs to be keept and which to be freed */
    if (tx->status == TX_COMMITTED) {
	release = tx->freed;
	keep = tx->allocated;
    } else if (tx->status == TX_ABORTED) {
	release = tx->allocated;
	keep = tx->freed;
    } else {
	return;
    }
    
    /* Free the memory and the mem_blocks of the memory which is no longer needed */
    next = release;
    while (next != NULL) {
	cur = next;
	next = next->next;
	free(cur->addr);
	free(cur);
    }

    /* Free the mem_blocks of the memory which is keept */
    next = keep;
    while (next != NULL) {
	cur = next;
	next = next->next;
	free(cur);
    }
    
    /* Reset the tansactional memory buffer */
    tx->freed = NULL;
    tx->allocated = NULL;
    
}
