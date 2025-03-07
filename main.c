#include "./submodules/cstd/lib.c"

#define METADATA_DELIMITER "---"
#define BACK_LINK "<p><a href=\"/blog\"> ⏴ Back to all articles</a></p>\n"

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
static PgString html_make_id(PgString s, PgAllocator *allocator) {
  Pgu8Dyn sb = {0};
  PG_DYN_ENSURE_CAP(&sb, s.len * 2, allocator);

  // TODO: UTF8.
  for (u64 i = 0; i < s.len; i++) {
    u8 c = PG_SLICE_AT(s, i);

    if (pg_character_is_alphanumeric(c)) {
      // TODO: u8 lowered = pg_xxx
      *PG_DYN_PUSH(&sb, allocator) = c;
    } else if ('+' == c) {
      PG_DYN_APPEND_SLICE(&sb, PG_S("plus"), allocator);
    } else if ('#' == c) {
      PG_DYN_APPEND_SLICE(&sb, PG_S("sharp"), allocator);
    } else {
      *PG_DYN_PUSH(&sb, allocator) = '-';
    }
  }

  return PG_DYN_SLICE(PgString, sb);
}

[[nodiscard]]
static PgString datetime_to_date(PgString datetime) {
  PgStringCut cut = pg_string_cut_byte(datetime, 'T');
  return cut.ok ? cut.left : datetime;
}

[[nodiscard]] static PgString markdown_to_html(PgString markdown,
                                               PgAllocator *allocator) {
  PgStringDyn args = {0};
  *PG_DYN_PUSH(&args, allocator) = PG_S("--validate-utf8");
  *PG_DYN_PUSH(&args, allocator) = PG_S("-e");
  *PG_DYN_PUSH(&args, allocator) = PG_S("table");
  *PG_DYN_PUSH(&args, allocator) = PG_S("-e");
  *PG_DYN_PUSH(&args, allocator) = PG_S("strikethrough");
  *PG_DYN_PUSH(&args, allocator) = PG_S("-e");
  *PG_DYN_PUSH(&args, allocator) = PG_S("footnotes");
  *PG_DYN_PUSH(&args, allocator) = PG_S("--unsafe");
  *PG_DYN_PUSH(&args, allocator) = PG_S("-t");
  *PG_DYN_PUSH(&args, allocator) = PG_S("html");

  PgRing ring_stdin = pg_ring_make(markdown.len, allocator);
  PG_ASSERT(ring_stdin.data.data);
  PG_ASSERT(true == pg_ring_write_slice(&ring_stdin, markdown));

  PgRing ring_stdout = pg_ring_make(512 * PG_KiB, allocator);
  PG_ASSERT(ring_stdout.data.data);

  PgRing ring_stderr = pg_ring_make(2048, allocator);
  PG_ASSERT(ring_stderr.data.data);

  PgProcessSpawnOptions options = {
      .ring_stdin = &ring_stdin,
      .ring_stdout = &ring_stdout,
      .ring_stderr = &ring_stderr,
  };
  PgProcessResult res_spawn = pg_process_spawn(
      PG_S("cmark-gfm"), PG_DYN_SLICE(PgStringSlice, args), options, allocator);
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

  return process_stdout;
}

static void article_generate_html_file(PgString markdown, Article *article,
                                       PgString header, PgString footer,
                                       PgAllocator *allocator) {
  Pgu8Dyn sb = {0};
  PG_DYN_ENSURE_CAP(&sb, markdown.len * 3, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<!DOCTYPE html>\n<html>\n<head>\n<title>"),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, pg_html_sanitize(article->title, allocator),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</title>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, header, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("\n<div class=\"article-prelude\">\n  "),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S(BACK_LINK), allocator);
  PG_DYN_APPEND_SLICE(
      &sb, PG_S("\n  <p class=\"publication-date\">Published on "), allocator);
  PG_DYN_APPEND_SLICE(&sb, datetime_to_date(article->creation_date), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</p>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</div>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<div class=\"article-title\">\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<h1>"), allocator);
  PG_DYN_APPEND_SLICE(&sb, article->title, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</h1>\n\n"), allocator);

  PG_DYN_APPEND_SLICE(&sb, PG_S("  <div class=\"tags\">"), allocator);
  for (u64 i = 0; i < article->tags.len; i++) {
    PgString tag = PG_SLICE_AT(article->tags, i);
    PgString id = html_make_id(tag, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S(" <a href=\"/blog/articles-by-tag.html#"),
                        allocator);
    PG_DYN_APPEND_SLICE(&sb, id, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("\" class=\"tag\">"), allocator);
    PG_DYN_APPEND_SLICE(&sb, tag, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("</a>"), allocator);
  }
  PG_DYN_APPEND_SLICE(&sb, PG_S("</div>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("  </div>\n"), allocator);

  // TODO: toc.
  PG_DYN_APPEND_SLICE(&sb, PG_S("\n"), allocator);

  PgString cmark_out = markdown_to_html(markdown, allocator);
  PG_DYN_APPEND_SLICE(&sb, cmark_out, allocator);

  PG_DYN_APPEND_SLICE(&sb, PG_S(BACK_LINK), allocator);
  PG_DYN_APPEND_SLICE(&sb, footer, allocator);

  PgString html = PG_DYN_SLICE(PgString, sb);
  PG_ASSERT(0 == pg_file_write_full(article->html_file_name, html, allocator));
}

[[nodiscard]]
static Article article_generate(PgString header, PgString footer,
                                GitStat git_stat, PgAllocator *allocator) {
  (void)header;
  (void)footer;
  printf("generating article: %.*s\n", (int)git_stat.path_rel.len,
         git_stat.path_rel.data);
  Article article = {
      .creation_date = git_stat.creation_date,
      .modification_date = git_stat.modification_date,
  };

  PgStringResult res_markdown =
      pg_file_read_full_from_path(git_stat.path_rel, allocator);
  PG_ASSERT(0 == res_markdown.err);
  PgString markdown = res_markdown.res;

  PgStringCut cut = pg_string_cut_string(markdown, PG_S(METADATA_DELIMITER));
  PG_ASSERT(cut.ok);
  PgString metadata_str = cut.left;
  PG_ASSERT(!pg_string_is_empty(metadata_str));
  PgString article_content = cut.right;
  PG_ASSERT(!pg_string_is_empty(article_content));

  cut = pg_string_cut_byte(metadata_str, '\n');
  PG_ASSERT(cut.ok);
  PgString metadata_title = cut.left;
  PG_ASSERT(!pg_string_is_empty(metadata_title));
  PgString metadata_tags = cut.right;
  PG_ASSERT(!pg_string_is_empty(metadata_tags));

  cut = pg_string_cut_byte(metadata_title, ':');
  PG_ASSERT(cut.ok);
  article.title = pg_string_trim(cut.right, ' ');
  PG_ASSERT(!pg_string_is_empty(article.title));

  cut = pg_string_cut_byte(metadata_tags, ':');
  PG_ASSERT(cut.ok);
  PgString tags_str = cut.right;
  PG_ASSERT(!pg_string_is_empty(tags_str));

  PgString remaining = tags_str;
  PgStringDyn tags = {0};
  PG_DYN_ENSURE_CAP(&tags, 32, allocator);
  for (;;) {
    cut = pg_string_cut_byte(remaining, ',');
    if (!cut.ok) {
      break;
    }

    PgString tag = pg_string_trim(cut.left, ' ');
    remaining = cut.right;

    *PG_DYN_PUSH_WITHIN_CAPACITY(&tags) = tag;
  }
  PG_ASSERT(tags.len > 0);
  article.tags = PG_DYN_SLICE(PgStringSlice, tags);

  PgString stem = pg_path_stem(git_stat.path_rel);
  article.html_file_name = pg_string_concat(stem, PG_S(".html"), allocator);

  article_generate_html_file(article_content, &article, header, footer,
                             allocator);

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

    // The home page is generate separately. The logic is different from an
    // article.
    if (pg_string_eq(git_stat.path_rel, PG_S("index.md"))) {
      continue;
    }

    // Skip the readme.
    if (pg_string_eq(git_stat.path_rel, PG_S("README.md"))) {
      continue;
    }

    // Skip the todo.
    if (pg_string_eq(git_stat.path_rel, PG_S("todo.md"))) {
      continue;
    }

    Article article = article_generate(header, footer, git_stat, allocator);
    *PG_DYN_PUSH_WITHIN_CAPACITY(&articles) = article;
  }

  return PG_DYN_SLICE(ArticleSlice, articles);
}

int main() {
  PgArena arena = pg_arena_make_from_virtual_mem(100 * PG_MiB);
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
