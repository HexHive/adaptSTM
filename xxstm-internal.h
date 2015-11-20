/* internal functions */
static bufferslab_t *alloc_slab(stm_tx_t *tx);
static void free_slabs(stm_tx_t *tx, bufferslab_t *free, bufferslab_t *last);

static inline void buf_acquire_all_locks(stm_tx_t *tx);
static inline stm_word_t buf_validate(stm_tx_t *tx);
static inline void buf_release_all_locks(stm_tx_t *tx, stm_word_t version);
static inline void buf_write_back(stm_tx_t *tx);
static inline void buf_reset(stm_tx_t *tx);
static inline writeset_t *buf_get_write_addr(stm_tx_t *tx, stm_word_t *addr, stm_word_t allocate, stm_word_t value);
static inline stm_word_t buf_check_read(stm_tx_t *tx, stm_word_t *addr);

static void lock_reset();
static inline stm_word_t lock_safe_get_value(stm_tx_t *tx, volatile stm_word_t *lock);
static inline void lock_acquire(stm_tx_t *tx, stm_word_t *addr);

static inline void cont_handle_conflict(stm_tx_t *tx, stm_tx_t *other);

static inline void mem_free_memory(stm_tx_t *tx);

/** Get a pointer to the transactional version of a shared address for reading */
volatile void* stm_get_read_addr   (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes) { return NULL; }
/** Get a pointer to the transactional version of a shared address for reading */
volatile void* stm_get_read2_addr   (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes) { return NULL; }
/** Get a pointer to the transactional version of a shared address for writing */
volatile void* stm_get_write_addr  (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes) { return NULL; }
/** Get a pointer to the transactional version of a shared address for modifying */
volatile void* stm_get_modify_addr (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes) { return NULL; }
/** Tells the STM that the writing/modiying is done */
void  stm_finish_writing  (stm_tx_t *tx, volatile void *addr, unsigned int num_bytes) {}

void stm_store2(stm_tx_t *tx, volatile stm_word_t *addr, stm_word_t value, stm_word_t mask) {
    stm_store(tx, (stm_word_t*)addr, value);
}

