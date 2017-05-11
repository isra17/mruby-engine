#include "memory_pool.h"
#include "mruby_engine.h"
#include "dlmalloc.h"
#undef NOINLINE
#include <ruby.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>

#if defined(MAP_ANONYMOUS)
#define ME_MAP_ANONYMOUS MAP_ANONYMOUS
#elif defined(MAP_ANON)
#define ME_MAP_ANONYMOUS MAP_ANON
#else
#error "this gem requires anonymous memory regions"
#endif

struct me_memory_pool {
  mspace mspace;
  uint8_t *start;
  size_t capacity;
  int verbose;
};

#define CAPACITY_MIN ((size_t)(256 * KiB))
#define CAPACITY_MAX ((size_t)(256 * MiB))
#define ALLOC_MAX ((size_t)(256 * MiB))

static size_t round_capacity(size_t capacity) {
  size_t page_size = (size_t)sysconf(_SC_PAGE_SIZE);
  size_t partial_page_p = capacity & (page_size - 1);
  if (partial_page_p)
    capacity = (capacity & ~(page_size - 1)) + page_size;
  return capacity;
}

struct me_memory_pool *me_memory_pool_new(size_t capacity, struct me_memory_pool_err *err, int verbose, void* base_address) {
  size_t rounded_capacity = round_capacity(capacity);
  if (rounded_capacity < CAPACITY_MIN || CAPACITY_MAX < rounded_capacity) {
    err->type = ME_MEMORY_POOL_INVALID_CAPACITY;
    err->data.invalid_capacity.min = CAPACITY_MIN;
    err->data.invalid_capacity.max = CAPACITY_MAX;
    err->data.invalid_capacity.capacity = capacity;
    err->data.invalid_capacity.rounded_capacity = rounded_capacity;
    return NULL;
  }

  uint8_t *bytes = mmap(base_address, rounded_capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | ME_MAP_ANONYMOUS, -1, 0);
  if (bytes == MAP_FAILED) {
    err->type = ME_MEMORY_POOL_SYSTEM_ERR;
    err->data.system_err.err_no = errno;
    err->data.system_err.capacity = capacity;
    err->data.system_err.rounded_capacity = rounded_capacity;
    return NULL;
  }

  mspace mspace = create_mspace_with_base(bytes, rounded_capacity, 0);
  mspace_set_footprint_limit(mspace, rounded_capacity);
  struct me_memory_pool *self = mspace_malloc(mspace, sizeof(struct me_memory_pool));
  self->mspace = mspace;
  self->start = bytes;
  self->capacity = rounded_capacity;
  self->verbose = 0;

  err->type = ME_MEMORY_POOL_NO_ERR;

  if (verbose) {
    printf("[*] Allocated memory pool at %p with size 0x%08zx\n", bytes, rounded_capacity);
  }

  return self;
}

struct meminfo me_memory_pool_info(struct me_memory_pool *self) {
  struct meminfo info;
  struct mallinfo dlinfo = mspace_mallinfo(self->mspace);
  info.arena = dlinfo.arena;
  info.hblkhd = dlinfo.hblkhd;
  info.uordblks = dlinfo.uordblks;
  info.fordblks = dlinfo.fordblks;
  return info;
}

size_t me_memory_pool_get_capacity(struct me_memory_pool *self) {
  return self->capacity;
}

void *me_memory_pool_malloc(struct me_memory_pool *self, size_t size) {
  void* data = mspace_malloc(self->mspace, size);
  if (self->verbose) {
    printf("[*] malloc(0x%08zx) -> %p\n", size, data);
  }
  return data;
}

void *me_memory_pool_realloc(struct me_memory_pool *self, void *block, size_t size) {
  void* new_block = mspace_realloc(self->mspace, block, size);
  if (self->verbose) {
    printf("[*] remalloc(%p, 0x%08zx) -> %p\n", block, size, new_block);
  }
  return new_block;
}

void me_memory_pool_free(struct me_memory_pool *self, void *block) {
  if (self->verbose) {
    printf("[*] free(%p)\n", block);
  }
  return mspace_free(self->mspace, block);
}

void me_memory_pool_verbose(struct me_memory_pool *self, int verbose) {
  self->verbose = verbose;
}

void me_memory_pool_destroy(struct me_memory_pool *self) {
  if (self->verbose) {
    printf("[*] destroying memory pool\n");
  }
  uint8_t *start = self->start;
  size_t capacity = self->capacity;
  destroy_mspace(self->mspace);
  munmap(start, capacity);
}
