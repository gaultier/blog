#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct {
  uint64_t in_use_space, in_use_objects, alloc_space, alloc_objects;
  uint64_t *call_stack;
  uint64_t call_stack_len;
} mem_record_t;

typedef struct mem_profile mem_profile_t;
typedef struct {
  uint8_t *start;
  uint8_t *end;
  mem_profile_t *profile;
} arena_t;

struct mem_profile {
  mem_record_t *records;
  uint64_t records_len;
  uint64_t records_cap;
  uint64_t in_use_space, in_use_objects, alloc_space, alloc_objects;
  arena_t arena;
};

static void *arena_alloc(arena_t *a, size_t size, size_t align, size_t count);

static uint8_t record_call_stack(uint64_t *dst, uint64_t cap) {
  uintptr_t *rbp = __builtin_frame_address(0);

  uint64_t len = 0;

  while (rbp != 0 && ((uint64_t)rbp & 7) == 0 && *rbp != 0) {
    const uintptr_t rip = *(rbp + 1);
    rbp = (uintptr_t *)*rbp;

    // `rip` points to the return instruction in the caller, once this call is
    // done. But: We want the location of the call i.e. the `call xxx`
    // instruction, so we subtract one byte to point inside it, which is not
    // quite 'at' it, but good enough.
    dst[len++] = rip - 1;

    if (len >= cap)
      return len;
  }
  return len;
}
static void mem_profile_record_alloc(mem_profile_t *profile,
                                     uint64_t objects_count,
                                     uint64_t bytes_count) {
  // Record the call stack by stack walking.
  uint64_t call_stack[64] = {0};
  uint64_t call_stack_len =
      record_call_stack(call_stack, sizeof(call_stack) / sizeof(call_stack[0]));

  // Update the sums.
  profile->alloc_objects += objects_count;
  profile->alloc_space += bytes_count;
  profile->in_use_objects += objects_count;
  profile->in_use_space += bytes_count;

  // Upsert the record.
  for (uint64_t i = 0; i < profile->records_len; i++) {
    mem_record_t *r = &profile->records[i];

    if (r->call_stack_len == call_stack_len &&
        memcmp(r->call_stack, call_stack, call_stack_len * sizeof(uint64_t)) ==
            0) {
      // Found an existing record, update it.
      r->alloc_objects += objects_count;
      r->alloc_space += bytes_count;
      r->in_use_objects += objects_count;
      r->in_use_space += bytes_count;
      return;
    }
  }

  // Not found, insert a new record.
  mem_record_t record = {
      .alloc_objects = objects_count,
      .alloc_space = bytes_count,
      .in_use_objects = objects_count,
      .in_use_space = bytes_count,
  };
  record.call_stack = arena_alloc(&profile->arena, sizeof(uint64_t),
                                  _Alignof(uint64_t), call_stack_len);
  memcpy(record.call_stack, call_stack, call_stack_len * sizeof(uint64_t));
  record.call_stack_len = call_stack_len;

  if (profile->records_len >= profile->records_cap) {
    uint64_t new_cap = profile->records_cap * 2;
    // Grow the array.
    mem_record_t *new_records = arena_alloc(
        &profile->arena, sizeof(mem_record_t), _Alignof(mem_record_t), new_cap);
    memcpy(new_records, profile->records,
           profile->records_len * sizeof(mem_record_t));
    profile->records_cap = new_cap;
    profile->records = new_records;
  }
  profile->records[profile->records_len++] = record;
}

static void mem_profile_write(mem_profile_t *profile, FILE *out) {
  fprintf(out, "heap profile: %lu: %lu [     %lu:    %lu] @ heapprofile\n",
          profile->in_use_objects, profile->in_use_space,
          profile->alloc_objects, profile->alloc_space);

  for (uint64_t i = 0; i < profile->records_len; i++) {
    mem_record_t r = profile->records[i];

    fprintf(out, "%lu: %lu [%lu: %lu] @ ", r.in_use_objects, r.in_use_space,
            r.alloc_objects, r.alloc_space);

    for (uint64_t j = 0; j < r.call_stack_len; j++) {
      fprintf(out, "%#lx ", r.call_stack[j]);
    }
    fputc('\n', out);
  }

  fputs("\nMAPPED_LIBRARIES:\n", out);

  static uint8_t mem[4096] = {0};
  int fd = open("/proc/self/maps", O_RDONLY);
  assert(fd != -1);
  ssize_t read_bytes = read(fd, mem, sizeof(mem));
  assert(read_bytes != -1);
  close(fd);

  fwrite(mem, 1, read_bytes, out);

  fflush(out);
}

static void *arena_alloc(arena_t *a, size_t size, size_t align, size_t count) {
  size_t available = a->end - a->start;
  size_t padding = -(size_t)a->start & (align - 1);

  size_t offset = padding + size * count;
  if (available < offset) {
    fprintf(stderr,
            "Out of memory: available=%lu "
            "allocation_size=%lu\n",
            available, offset);
    abort();
  }

  uint8_t *res = a->start + padding;

  a->start += offset;

  if (a->profile) {
    mem_profile_record_alloc(a->profile, count, offset);
  }

  return (void *)res;
}

static arena_t arena_new(uint64_t cap, mem_profile_t *profile) {
  uint8_t *mem = mmap(NULL, cap, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  assert(mem != (void *)-1);

  arena_t arena = {
      .profile = profile,
      .start = mem,
      .end = mem + cap,
  };
  return arena;
}

void b(int n, arena_t *arena) {
  arena_alloc(arena, sizeof(int), _Alignof(int), n);
}

void a(int n, arena_t *arena) {
  arena_alloc(arena, sizeof(int), _Alignof(int), n);
  b(n, arena);
}

int main() {
  arena_t mem_profile_arena = arena_new(1 << 16, NULL);
  mem_profile_t mem_profile = {
      .arena = mem_profile_arena,
      .records = arena_alloc(&mem_profile_arena, sizeof(mem_record_t),
                             _Alignof(mem_record_t), 16),
      .records_cap = 16,
  };

  arena_t arena = arena_new(1 << 28, &mem_profile);

  for (int i = 0; i < 2; i++)
    a(2 * 1024 * 1024, &arena);

  b(3 * 1024 * 1024, &arena);

  mem_profile_write(&mem_profile, stderr);
}
