# Path to fastSTM root directory
ROOT ?= .

CC = gcc
AR = ar

#LIB_TCMALLOC=./malloc/google-perftools-1.1/local/

# common
CFLAGS += -ffast-math -g -O3 -malign-double -pipe -finline-limit=20000 -fomit-frame-pointer -finline-functions
CFLAGS += -Winline -finline-limit=30000 --param inline-unit-growth=512 --param large-function-growth=2048 -funit-at-a-time
CFLAGS += -Wall -Wno-unused-function -fno-strict-aliasing -funroll-loops -D_REENTRANT -D_THREAD_SAFE -msse2

#2CFLAGS += -ftree-vectorize -msse3 -march=nocona -m64 -mno-push-args -fpie
CFLAGS += -fno-defer-pop -ftree-ccp -ftree-ter -ftree-lrs -ftree-sra -ftree-copyrename -ftree-fre -ftree-ch -fmerge-constants -fcrossjumping -fcse-skip-blocks -fgcse -fexpensive-optimizations -fschedule-insns2 -fregmove -freorder-blocks -fthread-jumps -fgcse-lm -freorder-blocks -freorder-functions -funit-at-a-time -falign-functions -falign-jumps -ftree-pre -fgcse-after-reload -fomit-frame-pointer -ffloat-store -fprefetch-loop-arrays -fmodulo-sched -fno-function-cse -fgcse-sm -freschedule-modulo-scheduled-loops -ftree-loop-im -mieee-fp -minline-all-stringops -mfpmath=sse,387 -funsafe-math-optimizations -fno-trapping-math 
CFLAGS += -freorder-blocks -fthread-jumps -freorder-functions -falign-jumps  -fprefetch-loop-arrays -funsafe-math-optimizations -fno-trapping-math -fschedule-insns2

#CFLAGS = -O1 -fno-if-conversion -fno-merge-constants -fno-split-wide-types -fno-tree-ch -fno-tree-copyrename -fno-tree-fre -fno-tree-ter -falign-labels -fcrossjumping -fcse-follow-jumps -fforward-propagate -fgcse -finline-small-functions -foptimize-register-move -foptimize-sibling-calls -fpeephole2 -freorder-blocks -fschedule-insns2 -fstrict-aliasing -ftree-vrp -fgcse-after-reload -finline-functions -ftree-vectorize -fpeel-loops -ftracer -funroll-all-loops -fgcse-las -ftree-loop-im -ftree-loop-ivcanon -fivopts

#CFLAGS = -O1 -fno-guess-branch-probability -fno-if-conversion -fno-merge-constants -fno-split-wide-types -fno-tree-sink -fno-tree-sra -falign-jumps -falign-labels -fcse-follow-jumps -fgcse -finline-small-functions -foptimize-register-move -freorder-blocks -fstrict-aliasing -fthread-jumps -ftree-store-ccp -finline-functions -fpredictive-commoning -ffloat-store -ftracer -fbranch-target-load-optimize -fmodulo-sched -finline-limit=700

#CFLAGS += -I$(LIB_TCMALLOC)/include 
CFLAGS += -I$(ROOT)/include
#LDFLAGS += -L$(LIB_TCMALLOC)/lib 
LDFLAGS += -L$(ROOT)/lib -lfastSTM -lpthread
# -ltcmalloc_minimal

# production
#CFLAGS += -O3 -DNDEBUG -g -DGLOBAL_STATS
#CFLAGS += -O3 -DNDEBUG -g

#CFLAGS += -DNDEBUG

# benchmarks
#CFLAGS += -DNDEBUG -g -DGLOBAL_STATS -DSTATS
CFLAGS += -DNDEBUG -g

# debugging
#CFLAGS += -O0 -ggdb -DDEBUG -DSTATS -DGLOBAL_STATS
#CFLAGS += -O0 -ggdb -DNDEBUG -DSTATS -DGLOBAL_STATS

# Eager or Lazy Locking?
CFLAGS += -DEAGER_LOCKING

# Use the adaptive subsystem?
CFLAGS += -DADAPTIVENESS

CFLAGS += -DWRITEBACK
CFLAGS += -DWRITETHROUGH

# change size of whash array dynamically
CFLAGS += -DADAPTIVE_WHASH

# switch hash function adaptively?
CFLAGS += -DADAPTIVEHASH
#CFLAGS += -DADAPTIVEWHASH2

# should the contention manager use exp. number of yield (exp dropoff)
CFLAGS += -DEXPDROPOFF

# should the clock be in one cache line?
CFLAGS += -DGLOBALCLOCKCACHELINE

# use a bloom filter for the write-set
CFLAGS += -DWRITEBLOOM

# Be really really safe (hurts performance!) but is 'more' correct and removes speculative reads
#CFLAGS += -DSAFE_MODE

# work around some valgrind bugs
#CFLAGS += -DVALGRIND

# profiling
#CFLAGS += -O0 -g -pg -DDEBUG -DSTATS
#LDFLAGS += -g -pg -fprofile-arcs -ftest-coverage


# optimizing but with asserts
#CFLAGS += -O4 -g -DDEBUG -DSTATS -DGLOBAL_STATS 

##################################
# variables
##################################
SRCDIR = $(ROOT)
LIBDIR = $(ROOT)/lib

CFLAGS += -I$(SRCDIR) $(MORECFLAGS)

LIBS = $(LIBDIR)/libfastSTM.a

STM = xxstm

.PHONY:	all clean tests

$(SRCDIR)/libfastSTM.o:	$(SRCDIR)/$(STM).c
	$(CC) $(CFLAGS) -c -o $@ -combine $^

$(LIBDIR)/libfastSTM.a:	$(SRCDIR)/libfastSTM.o 
#$(LIB_TCMALLOC)/lib/libtcmalloc_minimal.so
	$(AR) cru $@ $^


##################################
# implementation
##################################

all:	$(LIBS)

%.bc:	%.c
	$(LLVMGCC) $(CFLAGS) -emit-llvm -c -o $@ $<

$(LIBDIR)/fastSTM.bc:	$(SRCDIR)/$(STM).bc
	$(LLVMLD) -link-as-library -o $@ $^

tests:	$(LIBS)
	$(MAKE) -C tests

install: all
	cp $(LIBDIR)/libfastSTM.a ../fastSTM/lib
docs:
	doxygen Doxyfile

clean:
	rm -f $(LIBS) $(TLIBS) $(SRCDIR)/*.o $(SRCDIR)/*.bc

cleanall:	clean
	TARGET=clean $(MAKE) -C tests
	rm -rf docs
