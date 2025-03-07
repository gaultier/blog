#include "./submodules/cstd/lib.c"

typedef struct {
  u32 value;
} Sha256Hash;

typedef struct Title Title;
struct Title {
  PgString title;
  PgString content_html_id;
  u8 level;
  Sha256Hash hash;
  Title *parent, *first_child, *next_sibling;
  u32 pos_start, pos_end;
};

typedef struct {
  PgString html_file_name;
  PgString title;
  PgStringSlice tags;
  PgString creation_date, modification_date;
  Title *titles_root;
} Article;
PG_DYN(Article) ArticleDyn;
PG_SLICE(Article) ArticleSlice;

typedef struct {
  PgString creation_date, modification_date, path_rel;
} GitStat;
PG_SLICE(GitStat) GitStatSlice;

[[nodiscard]]
static GitStatSlice git_get_articles_stats(PgAllocator *allocator) {
  GitStatSlice res = {0};

  PgStringDyn args = {0};
  *PG_DYN_PUSH(&args, allocator) = PG_S("log");
  // Print the date in ISO format.
  *PG_DYN_PUSH(&args, allocator) = PG_S("--format='%aI'");
  // Ignore merge commits since they do not carry useful information.
  *PG_DYN_PUSH(&args, allocator) = PG_S("--no-merges");
  // Only interested in creation, modification, renaming, deletion.
  *PG_DYN_PUSH(&args, allocator) = PG_S("--diff-filter=AMRD");
  // Show which modification took place:
  // A: added, M: modified, RXXX: renamed (with percentage score), etc.
  *PG_DYN_PUSH(&args, allocator) = PG_S("--name-status");
  *PG_DYN_PUSH(&args, allocator) = PG_S("--reverse");
  *PG_DYN_PUSH(&args, allocator) = PG_S("*.md");

  PgRing ring_stdout = pg_ring_make(512 * PG_KiB, allocator);
  PG_ASSERT(ring_stdout.data.data);

  PgRing ring_stderr = pg_ring_make(2048, allocator);
  PG_ASSERT(ring_stderr.data.data);

  PgProcessSpawnOptions options = {
      .ring_stdout = &ring_stdout,
      .ring_stderr = &ring_stderr,
  };
  PgProcessResult res_spawn = pg_process_spawn(
      PG_S("git"), PG_DYN_SLICE(PgStringSlice, args), options, allocator);
  PG_ASSERT(0 == res_spawn.err);

  PgProcess process = res_spawn.res;

  PG_ASSERT(0 == pg_process_capture_std_io(process));

  PgProcessExitResult res_wait = pg_process_wait(process);
  PG_ASSERT(0 == res_wait.err);

  PgProcessStatus status = res_wait.res;
  PG_ASSERT(0 == status.exit_status);
  PG_ASSERT(0 == status.signal);
  PG_ASSERT(status.exited);
  PG_ASSERT(!status.signaled);
  PG_ASSERT(!status.core_dumped);
  PG_ASSERT(!status.stopped);

  PgString process_stdout =
      pg_string_make(pg_ring_read_space(ring_stdout), allocator);
  PG_ASSERT(true == pg_ring_read_slice(&ring_stdout, process_stdout));

  PG_ASSERT(0 == pg_ring_read_space(ring_stderr));
  return res;
}

[[nodiscard]]
static ArticleSlice articles_generate(PgString header, PgString footer,
                                      PgAllocator *allocator) {
  PG_ASSERT(!pg_string_is_empty(header));
  PG_ASSERT(!pg_string_is_empty(footer));

  GitStatSlice git_stats = git_get_articles_stats(allocator);
  (void)git_stats;

  ArticleDyn articles = {0};
  PG_DYN_ENSURE_CAP(&articles, 100, allocator);

  return PG_DYN_SLICE(ArticleSlice, articles);
}

int main() {
  PgArena arena = pg_arena_make_from_virtual_mem(10 * PG_MiB);
  PgArenaAllocator arena_allocator = pg_make_arena_allocator(&arena);
  PgAllocator *allocator = pg_arena_allocator_as_allocator(&arena_allocator);

  PgStringResult res_header =
      pg_file_read_full_from_path(PG_S("header.html"), allocator);
  PG_ASSERT(0 == res_header.err);
  PgString header = res_header.res;

  PgStringResult res_footer =
      pg_file_read_full_from_path(PG_S("footer.html"), allocator);
  PG_ASSERT(0 == res_footer.err);
  PgString footer = res_footer.res;

  ArticleSlice articles = articles_generate(header, footer, allocator);
  (void)articles;
}
