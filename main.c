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
PG_DYN(GitStat) GitStatDyn;

[[nodiscard]] static i64 git_stats_find_by_path_rel(GitStatSlice git_stats,
                                                    PgString path_rel) {
  for (u64 i = 0; i < git_stats.len; i++) {
    GitStat elem = PG_SLICE_AT(git_stats, i);
    if (pg_string_eq(elem.path_rel, path_rel)) {
      return (i64)i;
    }
  }

  return -1;
}

[[nodiscard]]
static GitStatSlice git_get_articles_stats(PgAllocator *allocator) {
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

  GitStatDyn stats = {0};
  PG_DYN_ENSURE_CAP(&stats, 256, allocator);

  // Sample git output:
  // 2024-10-31T16:09:02+01:00
  //
  // M       lessons_learned_from_a_successful_rust_rewrite.md
  // A       tip_of_day_3.md
  // 2025-02-18T08:07:55+01:00
  //
  // R100    sha.md  making_my_debug_build_run_100_times_faster.md

  // For each commit.
  PgString remaining = process_stdout;
  for (;;) {
    PgString date = {0};
    {
      PgStringCut cut = pg_string_cut_byte(remaining, '\n');
      if (!cut.ok) { // End.
        break;
      }
      remaining = cut.right;

      PG_ASSERT(pg_string_starts_with(cut.left, PG_S("'20")));
      date = cut.left;
      date = pg_string_trim(date, '\'');
      date = pg_string_trim(date, '\n');
    }
    // Empty line.
    {
      PgStringCut cut = pg_string_cut_byte(remaining, '\n');
      PG_ASSERT(cut.ok);
      remaining = cut.right;

      PG_ASSERT(pg_string_is_empty(cut.left));
    }

    // Files.
    for (;;) {
      // Start of a new commit?
      if (pg_string_starts_with(remaining, PG_S("'20"))) {
        break;
      }

      PgStringCut cut = pg_string_cut_byte(remaining, '\n');
      if (!cut.ok) {
        break;
      }
      PgString line = cut.left;
      remaining = cut.right;

      PG_ASSERT(!pg_string_is_empty(line));
      u8 action = PG_SLICE_AT(line, 0);
      PG_ASSERT(action == 'A' || action == 'M' || action == 'R' ||
                action == 'D');

      PgString path_old = {0}, path_new = {0};
      {
        // Skip the 'action' part.
        cut = pg_string_cut_byte(line, '\t');
        PG_ASSERT(cut.ok);
        line = cut.right;

        cut = pg_string_cut_byte(line, '\t');
        path_old = cut.ok ? cut.left : line;
        path_new = cut.ok ? cut.right : path_old;
        PG_ASSERT(!pg_string_is_empty(path_old));
        PG_ASSERT(!pg_string_is_empty(path_new));
      }

      if ('D' == action) {
        i64 idx = git_stats_find_by_path_rel(PG_DYN_SLICE(GitStatSlice, stats),
                                             path_old);
        PG_ASSERT(idx >= 0);
        PG_SLICE_SWAP_REMOVE(&stats, idx);
        continue;
      }

      if ('R' == action) {
        i64 idx = git_stats_find_by_path_rel(PG_DYN_SLICE(GitStatSlice, stats),
                                             path_old);
        PG_ASSERT(idx >= 0);
        PG_SLICE_SWAP_REMOVE(&stats, idx);

        // Still need to insert the new entry below.
      }

      // TODO: upsert entry.
      i64 idx = git_stats_find_by_path_rel(PG_DYN_SLICE(GitStatSlice, stats),
                                           path_new);
      if (-1 == idx) {
        GitStat new_entry = {
            .creation_date = date,
            .modification_date = date,
            .path_rel = path_new,
        };
        *PG_DYN_PUSH_WITHIN_CAPACITY(&stats) = new_entry;
      } else {
        GitStat *entry = PG_SLICE_AT_PTR(&stats, idx);
        PG_ASSERT(!pg_string_is_empty(entry->creation_date));
        PG_ASSERT(!pg_string_is_empty(entry->modification_date));
        PG_ASSERT(
            PG_STRING_CMP_GREATER !=
            pg_string_cmp(entry->creation_date, entry->modification_date));
        // Keep updating the modification date, when we reach the end of the
        // commit log, it has the right value.
        entry->modification_date = date;
        PG_ASSERT(
            PG_STRING_CMP_GREATER !=
            pg_string_cmp(entry->creation_date, entry->modification_date));
      }
    }
  }

  return PG_DYN_SLICE(GitStatSlice, stats);
}

[[nodiscard]]
static Article article_generate(PgString header, PgString footer,
                                GitStat git_stat, PgAllocator *allocator) {
  (void)header;
  (void)footer;
  (void)allocator;
  printf("[D001] generating article: %.*s\n", (int)git_stat.path_rel.len,
         git_stat.path_rel.data);
  Article article = {
      .creation_date = git_stat.creation_date,
      .modification_date = git_stat.modification_date,
  };
  return article;
}

[[nodiscard]]
static ArticleSlice articles_generate(PgString header, PgString footer,
                                      PgAllocator *allocator) {
  PG_ASSERT(!pg_string_is_empty(header));
  PG_ASSERT(!pg_string_is_empty(footer));

  GitStatSlice git_stats = git_get_articles_stats(allocator);

  ArticleDyn articles = {0};
  PG_DYN_ENSURE_CAP(&articles, 100, allocator);

  for (u64 i = 0; i < git_stats.len; i++) {
    GitStat git_stat = PG_SLICE_AT(git_stats, i);
    Article article = article_generate(header, footer, git_stat, allocator);
    *PG_DYN_PUSH_WITHIN_CAPACITY(&articles) = article;
  }

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
