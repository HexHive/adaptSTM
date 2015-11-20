/**
 * This file contains the interface of adaptSTM
 * It is the only file which needs to be included from an application
 *
 * Copyright (c) 2010 ETH Zurich
 *   Mathias Payer <mathias.payer@inf.ethz.ch>
 *
 * With help by
 *   Stephan Classen <scl@soft-eng.ch>
 *   Peter Suter <suterpet@student.ethz.ch>
 *   Oliver Saurer <saurero@student.ethz.ch>
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


#ifndef ADAPTSTMEXT_H
#define ADAPTSTMEXT_H


#include <setjmp.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif



/*******************************************************************\
 *  TYPES                                                          *
\*******************************************************************/

/** STM word is used to pass data to fastSTM */
typedef uintptr_t stm_word_t;

/** Transaction descriptor */
typedef struct stm_tx stm_tx_t;



/*******************************************************************\
 *  FUNCTIONS                                                      *
\*******************************************************************/

/** Inits the whole STM system */
void stm_init(void);
/** Frees the datastructure of the STM */
void stm_exit(void);


/** Creates a new transaction descriptor */
stm_tx_t *stm_new(void);
/** Deletes the transaction descriptor and frees the memory it occupies */
void stm_delete(stm_tx_t *tx);


/** Starts a transaction */
void stm_start(stm_tx_t *tx, jmp_buf *env);
/** Commits a transaction */
void stm_commit(stm_tx_t *tx);
/** Retries the transaction */
void stm_retry(stm_tx_t *tx);
/**
 * Aborts a transaction -> this will discared any changes to the shared memory and
 * continues executing after the function call.
 */
void stm_abort(stm_tx_t *tx);


/** Reads a shared address and returns its value */
stm_word_t stm_load(stm_tx_t *tx, volatile stm_word_t *addr);

/** Stores a value to a shared address */
void stm_store(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value);
void stm_store2(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask);


/** Allocates memory */
void *stm_malloc(stm_tx_t *tx, size_t size);
/** Frees memory */
void stm_free(stm_tx_t *tx, void *addr);
/** Reallocates memory */
void *stm_realloc(stm_tx_t *tx, void *addr, size_t size);


/** Get a pointer to the transactional version of a shared address for reading */
volatile void* stm_get_read_addr   (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes);
/** Get a pointer to the transactional version of a shared address for reading */
volatile void* stm_get_read2_addr   (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes);
/** Get a pointer to the transactional version of a shared address for writing */
volatile void* stm_get_write_addr  (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes);
/** Get a pointer to the transactional version of a shared address for modifying */
volatile void* stm_get_modify_addr (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes);
/** Tells the STM that the writing/modiying is done */
void  stm_finish_writing  (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes);


/** Gets the transaction descriptor of the current thread */
stm_tx_t *stm_get_tx();
/** Get the jump buffer of the current thread */
jmp_buf *stm_get_env(stm_tx_t *tx);


/** Returns true if the current thread is in a transaction */
int stm_in_transaction(stm_tx_t *tx);


/** Get statistic from the current thread */
int stm_get_parameter(stm_tx_t *tx, const char *key, void *val);


/**
 * Mark a region of the memory as stack.
 * Loads and Stores in this region are not tracked by the STM
 */
void stm_set_stack_area(stm_tx_t *tx, void *start, void *end);

/**
 * Mark the bottom most scope that the transaction will reach.
 * If this is not called before stm_start, exiting that scope before the transaction ends is an error.
 * Requires NULL to be passed as env to stm_start.
 */
void stack_mark(stm_tx_t *tx, int stack_frame, int grow);

/**
 * Removes previous marks.
 */
void stack_unmark(stm_tx_t *tx);

/**
 * stack_mark has been called
 */
int stack_is_marked(stm_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* ADAPTSTMEXT_H */

