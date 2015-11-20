/**
 * This file contains the implementation of CAS and FETCH AND ADD
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

