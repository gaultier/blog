#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cmark-gfm.h"
#include "cmark-gfm-extension_api.h"

// One arena per thread, so that threads can parse concurrently (see also
// `SPECIAL_CHARS` in `inlines.c`).
static _Thread_local struct arena_chunk {
  size_t sz, used;
  uint8_t push_point;
  void *ptr;
  struct arena_chunk *prev;
} *A = NULL;

// The chunk `init_arena` allocated, which `cmark_arena_reset` keeps.
static _Thread_local struct arena_chunk *first = NULL;

// `_Thread_local` has no destructor: this key frees a thread's chunks when it
// exits. Its value is always `A`, the newest chunk, from which every other one
// is reachable, so the destructor needs no thread-local storage, which may
// already be gone by then.
static pthread_key_t arena_key;
static pthread_once_t arena_key_once = PTHREAD_ONCE_INIT;

static void free_chunks(void *head) {
  struct arena_chunk *c = (struct arena_chunk *)head;
  while (c) {
    struct arena_chunk *n = c->prev;
    free(c->ptr);
    free(c);
    c = n;
  }
}

static void make_arena_key(void) {
  if (pthread_key_create(&arena_key, free_chunks))
    abort();
}

static void set_head(struct arena_chunk *c) {
  A = c;
  if (pthread_setspecific(arena_key, c))
    abort();
}

static struct arena_chunk *alloc_arena_chunk(size_t sz, struct arena_chunk *prev) {
  struct arena_chunk *c = (struct arena_chunk *)calloc(1, sizeof(*c));
  if (!c)
    abort();
  c->sz = sz;
  c->ptr = calloc(1, sz);
  if (!c->ptr)
    abort();
  c->prev = prev;
  return c;
}

void cmark_arena_push(void) {
  if (!A)
    return;
  A->push_point = 1;
  set_head(alloc_arena_chunk(10240, A));
}

int cmark_arena_pop(void) {
  if (!A)
    return 0;
  struct arena_chunk *head = A;
  while (head && !head->push_point) {
    free(head->ptr);
    struct arena_chunk *n = head->prev;
    free(head);
    head = n;
  }
  if (head)
    head->push_point = 0;
  set_head(head);
  return 1;
}

static void init_arena(void) {
  pthread_once(&arena_key_once, make_arena_key);
  first = alloc_arena_chunk(4 * 1048576, NULL);
  set_head(first);
}

// Free every chunk but the first one, which is kept, zeroed, for the next
// document: allocating and faulting in a fresh one for each document costs more
// than the parse of a small one.
void cmark_arena_reset(void) {
  struct arena_chunk *c = A;
  while (c) {
    struct arena_chunk *n = c->prev;
    if (c != first) {
      free(c->ptr);
      free(c);
    }
    c = n;
  }
  if (first) {
    memset(first->ptr, 0, first->used);
    first->used = 0;
    first->push_point = 0;
    first->prev = NULL;
    set_head(first);
  }
}

static void *arena_calloc(size_t nmem, size_t size) {
  if (!A)
    init_arena();

  size_t sz = nmem * size + sizeof(size_t);

  // Round allocation sizes to largest integer size to
  // ensure returned memory is correctly aligned
  const size_t align = sizeof(size_t) - 1;
  sz = (sz + align) & ~align;

  struct arena_chunk *chunk;
  if (sz > A->sz) {
    A->prev = chunk = alloc_arena_chunk(sz, A->prev);
  } else if (sz > A->sz - A->used) {
    set_head(chunk = alloc_arena_chunk(A->sz + A->sz / 2, A));
  } else {
    chunk = A;
  }
  void *ptr = (uint8_t *) chunk->ptr + chunk->used;
  chunk->used += sz;
  *((size_t *) ptr) = sz - sizeof(size_t);
  return (uint8_t *) ptr + sizeof(size_t);
}

static void *arena_realloc(void *ptr, size_t size) {
  if (!A)
    init_arena();

  if (ptr) {
    size_t old_size = ((size_t *) ptr)[-1];
    if (size <= old_size)
      return ptr;

    // A buffer that grows (e.g. a line being read) is usually the last thing
    // allocated in the current chunk: extend it rather than copy it.
    if ((uint8_t *) ptr + old_size == (uint8_t *) A->ptr + A->used) {
      const size_t align = sizeof(size_t) - 1;
      size_t extra = ((size - old_size) + align) & ~align;
      if (extra <= A->sz - A->used) {
        A->used += extra;
        ((size_t *) ptr)[-1] = old_size + extra;
        return ptr;
      }
    }
  }

  void *new_ptr = arena_calloc(1, size);
  if (ptr)
    memcpy(new_ptr, ptr, ((size_t *) ptr)[-1]);
  return new_ptr;
}

static void arena_free(void *ptr) {
  (void) ptr;
  /* no-op */
}

cmark_mem CMARK_ARENA_MEM_ALLOCATOR = {arena_calloc, arena_realloc, arena_free};

cmark_mem *cmark_get_arena_mem_allocator(void) {
  return &CMARK_ARENA_MEM_ALLOCATOR;
}
