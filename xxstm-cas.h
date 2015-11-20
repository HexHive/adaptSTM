/**
 * Implements a basic compare and swap.
 * Sets: *addr = (*addr==old) ? *addr : old;
 * Return: 1 - ok, 0 - error
 */
inline stm_word_t CAS(volatile stm_word_t *addr, stm_word_t old, stm_word_t new_val)
{
  char result;
#ifdef __LP64__
  __asm__ __volatile__("lock; cmpxchgq %3, %0; setz %1"
#else
  __asm__ __volatile__("lock; cmpxchgl %3, %0; setz %1"
#endif
		       : "=m"(*addr), "=q"(result)
		       : "m"(*addr), "r" (new_val), "a"(old) : "memory");
  return (stm_word_t)result;
}

inline stm_word_t __always_inline FETCH_ADD(volatile stm_word_t *p, stm_word_t incr)
{
  stm_word_t result;
#ifdef __LP64__
  __asm__ __volatile__ ("lock; xaddq %0, %1" :
#else
  __asm__ __volatile__ ("lock; xaddl %0, %1" :
#endif
			"=r" (result), "=m" (*p) : "0" (incr), "m" (*p)
			: "memory");
  return result;
}

