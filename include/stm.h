/* =============================================================================
 *
 * stm.h
 *
 * User program interface of adaptSTM for STAMP.
 *
 * =============================================================================
 *
 * Author: Chi Cao Minh
 * Updated for adaptSTM by Mathias Payer
 *
 * =============================================================================
 */


#ifndef STM_H
#define STM_H 1


#include "adaptstm-external.h"
#include "util.h"
#include <setjmp.h>



#define STM_THREAD_T                    stm_tx_t
#define STM_SELF                        tx

#define STM_MALLOC(size)                stm_malloc(STM_SELF, size)
#define STM_FREE(ptr)                   stm_free(STM_SELF, ptr)

#define STM_RESTART()                   stm_retry(STM_SELF)

#define STM_STARTUP()                   stm_init()
#define STM_SHUTDOWN()                  stm_exit()

#define STM_NEW_THREAD()                stm_new()
#define STM_FREE_THREAD()               stm_delete(STM_SELF)
#define STM_BEGIN()                     do { \
                                            sigjmp_buf *buf = stm_get_env(tx); \
                                            sigsetjmp(*buf, 1); \
                                            stm_start(STM_SELF, buf); \
                                        } while (0)


#define STM_END()                       stm_commit(STM_SELF)

typedef volatile stm_word_t word;

#define STM_READ(var)                   stm_load(STM_SELF, (word*)(void*)&(var))
#define STM_READ_F(var)                 W2F(STM_READ(var))
#define STM_READ_P(var)                 W2VP(STM_READ(var))

#define STM_WRITE(var, val)             ({stm_store(STM_SELF, (word*)(void*)&(var), (word)(val)); var;})
#define STM_WRITE_F(var, val)           STM_WRITE(var, F2W(val,(word*)(void*)&(var)))
#define STM_WRITE_P(var, val)           STM_WRITE(var, VP2W(val))

#define STM_LOCAL_WRITE(var, val)       ({var = val; var;})
#define STM_LOCAL_WRITE_F(var, val)     ({var = val; var;})
#define STM_LOCAL_WRITE_P(var, val)     ({var = val; var;})


#endif /* STM_H */


/* =============================================================================
 *
 * End of stm.h
 *
 * =============================================================================
 */
