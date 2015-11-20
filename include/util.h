/* =============================================================================
 *
 * util.h
 *
 * Collection of useful utility routines
 *
 * Definitions of W2F, F2W conversions originally from TL2, all copyrights apply
 * =============================================================================
 */


#ifndef UTIL_H
#define UTIL_H


#include <assert.h>
#include <stdint.h>
#include "adaptstm-external.h"


#ifdef __cplusplus
extern "C" {
#endif


#define DIM(A)                          (sizeof(A)/sizeof((A)[0]))
#define UNS(a)                          ((ustm_word_t)(a))
#define ASSERT(x)                       /* assert(x) */
#define CTASSERT(x)                     ({ int a[1-(2*!(x))]; a[0] = 0;})


/*
 * Shorthand for type conversion routines
 */

#define W2F(v)                         word2float(v)
#define F2W(v,a)                         float2word(v,a)

#define WP2FP(v)                       wordp2floatpp(v)
#define FP2WP(v)                       floatp2wordp(v)

#define W2VP(v)                        word2voidp(v)
#define VP2W(v)                        voidp2word(v)


/* =============================================================================
 * word2float
 * =============================================================================
 */
static __inline__ float
word2float (stm_word_t val)
{
#ifdef __LP64__
    union {
        stm_word_t i;
        float f[2];
    } convert;
    convert.i = val;
    return convert.f[0];
#else
    union {
        stm_word_t i;
        float f;
    } convert;
    convert.i = val;
    return convert.f;
#endif
}


/* =============================================================================
 * float2word
 * =============================================================================
 */
static __inline__ stm_word_t
float2word (float val, stm_word_t* addr)
{
#ifdef __LP64__
    union {
        stm_word_t i;
        float f[2];
    } convert;
    convert.i = *addr;
    convert.f[0] = val;
    return convert.i;
#else
    union {
        stm_word_t i;
        float f;
    } convert;
    convert.f = val;
    return convert.i;
#endif
}


/* =============================================================================
 * wordp2floatp
 * =============================================================================
 */
static __inline__ float*
wordp2floatp (stm_word_t* val)
{
    union {
        stm_word_t* i;
        float* f;
    } convert;
    convert.i = val;
    return convert.f;
}


/* =============================================================================
 * floatp2wordp
 * =============================================================================
 */
static __inline__ stm_word_t*
floatp2wordp (float* val)
{
    union {
        stm_word_t* i;
        float* f;
    } convert;
    convert.f = val;
    return convert.i;
}


/* =============================================================================
 * word2voidp
 * =============================================================================
 */
static __inline__ void*
word2voidp (stm_word_t val)
{
    union {
        stm_word_t i;
        void* v;
    } convert;
    convert.i = val;
    return convert.v;
}


/* =============================================================================
 * voidp2word
 * =============================================================================
 */
static __inline__ stm_word_t
voidp2word (void* val)
{
    union {
        stm_word_t i;
        void* v;
    } convert;
    convert.v = val;
    return convert.i;
}


/* =============================================================================
 * CompileTimeAsserts
 *
 * Establish critical invariants and fail at compile-time rather than run-time
 * =============================================================================
 */
static __inline__ void
CompileTimeAsserts ()
{
#ifdef __LP64__
    CTASSERT(sizeof(stm_word_t) == sizeof(long));
    CTASSERT(sizeof(long) == 8);
#else
    CTASSERT(sizeof(stm_word_t) == sizeof(long));
    CTASSERT(sizeof(long) == 4);
#endif

    /*
     * For type conversions
     */
#ifdef __LP64__
    CTASSERT(2*sizeof(float) == sizeof(stm_word_t));
#else
    CTASSERT(sizeof(float) == sizeof(stm_word_t));
#endif
    CTASSERT(sizeof(float*) == sizeof(stm_word_t));
    CTASSERT(sizeof(void*) == sizeof(stm_word_t));
}


#ifdef __cplusplus
}
#endif


#endif /* UTIL_H */
