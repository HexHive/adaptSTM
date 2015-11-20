/**
 * Used for debugging.
 * Debugging is implemented as macros.
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
 
#ifdef DEBUG
  #define DEBUG_OUTPUT_FILE "/tmp/adaptstm.out"
  pthread_mutex_t debug_mutex;
  pthread_key_t thread_debug;
  FILE* debugStream;
  volatile int dodebug=1;
  #define DEBUG_START \
  do { \
  debugStream = fopen(DEBUG_OUTPUT_FILE,"w"); \
  pthread_mutex_init(&debug_mutex, NULL); \
  } while(0);

  #define DEBUG_END \
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
  // no debugging
  #define DPRINTF(...)
  #define DEBUG_START
  #define DEBUG_END
#endif

