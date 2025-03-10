#include "./submodules/cstd/lib.c"

#define FEED_UUID "9c065c53-31bc-4049-a795-936802a6b1df"
#define BASE_URL "https://gaultier.github.io/blog"
#define METADATA_DELIMITER "---"
#define BACK_LINK "<p><a href=\"/blog\"> ⏴ Back to all articles</a></p>\n"

typedef u32 TitleHash;

typedef struct Title Title;
struct Title {
  PgString title;
  PgString content_html_id;
  u8 level;
  TitleHash hash;
  Title *parent, *first_child, *next_sibling;
  u32 pos_start, pos_end;
};
PG_DYN(Title) TitleDyn;

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

static int article_cmp_by_creation_date_asc(const void *a, const void *b) {
  const Article *article_a = a;
  const Article *article_b = b;

  return pg_string_cmp(article_a->creation_date, article_b->creation_date);
}

static int article_cmp_by_creation_date_desc(const void *a, const void *b) {
  const Article *article_a = a;
  const Article *article_b = b;

  return pg_string_cmp(article_b->creation_date, article_a->creation_date);
}

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

  PgProcessSpawnOptions options = {
      .stdout_capture = PG_CHILD_PROCESS_STD_IO_PIPE,
      .stderr_capture = PG_CHILD_PROCESS_STD_IO_PIPE,
  };
  PgProcessResult res_spawn = pg_process_spawn(
      PG_S("git"), PG_DYN_SLICE(PgStringSlice, args), options, allocator);
  PG_ASSERT(0 == res_spawn.err);

  PgProcess process = res_spawn.res;

  PgProcessExitResult res_wait = pg_process_wait(process, allocator);
  PG_ASSERT(0 == res_wait.err);

  PgProcessStatus status = res_wait.res;
  PG_ASSERT(0 == status.exit_status);
  PG_ASSERT(0 == status.signal);
  PG_ASSERT(status.exited);
  PG_ASSERT(!status.signaled);
  PG_ASSERT(!status.core_dumped);
  PG_ASSERT(!status.stopped);

  PG_ASSERT(!pg_string_is_empty(status.stdout_captured));
  PG_ASSERT(pg_string_is_empty(status.stderr_captured));

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
  PgString remaining = status.stdout_captured;
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
        PG_ASSERT(PG_CMP_GREATER != pg_string_cmp(entry->creation_date,
                                                  entry->modification_date));
        // Keep updating the modification date, when we reach the end of the
        // commit log, it has the right value.
        entry->modification_date = date;
        PG_ASSERT(PG_CMP_GREATER != pg_string_cmp(entry->creation_date,
                                                  entry->modification_date));
      }
    }
  }

  return PG_DYN_SLICE(GitStatSlice, stats);
}

[[nodiscard]]
static PgString html_make_id(PgString s, PgAllocator *allocator) {
  PG_ASSERT(!pg_string_is_empty(s));

  Pgu8Dyn sb = {0};
  PG_DYN_ENSURE_CAP(&sb, s.len * 2, allocator);

  // TODO: UTF8.
  for (u64 i = 0; i < s.len; i++) {
    u8 c = PG_SLICE_AT(s, i);

    if (pg_character_is_alphanumeric(c)) {
      // FIXME
      u8 lowered = ('A' <= c && c <= 'Z') ? c + ('a' - 'A') : c;
      *PG_DYN_PUSH(&sb, allocator) = lowered;
    } else if ('+' == c) {
      PG_DYN_APPEND_SLICE(&sb, PG_S("plus"), allocator);
    } else if ('#' == c) {
      PG_DYN_APPEND_SLICE(&sb, PG_S("sharp"), allocator);
    } else if (i < s.len - 1 && sb.len > 0 && PG_SLICE_LAST(sb) != '-') {
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

[[nodiscard]] static PgString markdown_to_html(PgFileDescriptor markdown_file,
                                               u64 metadata_offset,
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

  PgProcessSpawnOptions options = {
      .stdin_capture = PG_CHILD_PROCESS_STD_IO_PIPE,
      .stdout_capture = PG_CHILD_PROCESS_STD_IO_PIPE,
      .stderr_capture = PG_CHILD_PROCESS_STD_IO_PIPE,
  };
  PgProcessResult res_spawn = pg_process_spawn(
      PG_S("cmark-gfm"), PG_DYN_SLICE(PgStringSlice, args), options, allocator);
  PG_ASSERT(0 == res_spawn.err);

  PgProcess process = res_spawn.res;

  PgU64Result res_markdown_file_size = pg_file_size(markdown_file);
  PG_ASSERT(0 == res_markdown_file_size.err);
  u64 to_copy = res_markdown_file_size.res - metadata_offset;
  Pgu64Ok offset = {.ok = true, .res = metadata_offset};
  PG_ASSERT(0 == pg_file_copy_with_descriptors(process.stdin_pipe,
                                               markdown_file, offset, to_copy));
  PG_ASSERT(0 == pg_file_close(process.stdin_pipe));
  process.stdin_pipe.fd = 0;

  PgProcessExitResult res_wait = pg_process_wait(process, allocator);
  PG_ASSERT(0 == res_wait.err);

  PgProcessStatus status = res_wait.res;
  PG_ASSERT(0 == status.exit_status);
  PG_ASSERT(0 == status.signal);
  PG_ASSERT(status.exited);
  PG_ASSERT(!status.signaled);
  PG_ASSERT(!status.core_dumped);
  PG_ASSERT(!status.stopped);

  PG_ASSERT(pg_string_is_empty(status.stderr_captured));
  return status.stdout_captured;
}

#define FNV_SEED ((u32)0x811c9dc5)

[[nodiscard]] static TitleHash title_compute_hash(Title *title, u32 hash) {
  // Reached root?
  if (title == title->parent) {
    return hash;
  }

  for (u64 i = 0; i < title->title.len; i++) {
    u8 c = PG_SLICE_AT(title->title, i);
    hash = (hash ^ (u32)c) * 0x01000193;
  }
  // Separator between titles.
  hash = (hash ^ (u32)'/') * 0x01000193;

  return title_compute_hash(title->parent, hash);
}

[[nodiscard]]
static Title *html_parse_titles(PgString html, PgAllocator *allocator) {
  TitleDyn titles = {0};
  PG_DYN_ENSURE_CAP(&titles, 64, allocator);

  Title *root = pg_alloc(allocator, sizeof(Title), _Alignof(Title), 1);
  root->level = 1;
  root->parent = root;

  u64 pos = 0;
  while (pos < html.len) {
    i64 idx_start =
        pg_string_indexof_string(PG_SLICE_RANGE_START(html, pos), PG_S("<h"));
    if (-1 == idx_start) {
      break;
    }

    u8 level_ch = PG_SLICE_AT(html, pos + (u64)idx_start + 2);
    if (!('2' <= level_ch && level_ch <= '6')) {
      pos += (u64)idx_start + 2;
      continue;
    }
    i64 idx_end = pg_string_indexof_string(
        PG_SLICE_RANGE_START(html, pos + (u64)idx_start), PG_S("</h"));
    PG_ASSERT(-1 != idx_end);

    PgString s = PG_SLICE_RANGE_START(html, pos + (u64)idx_start);
    s = PG_SLICE_RANGE(s, 0, (u64)idx_end);

    PG_ASSERT(pg_string_starts_with(s, PG_S("<h")));

    u8 level = PG_SLICE_AT(s, 2) - '0';
    PG_ASSERT(1 < level && level <= 6);

    PgString title_content = pg_string_trim_space(PG_SLICE_RANGE_START(s, 4));

    Title title = {
        .title = title_content,
        .content_html_id = html_make_id(title_content, allocator),
        .level = level,
        .pos_start = (u32)pos + (u32)idx_start,
        .pos_end = (u32)pos + (u32)idx_start + (u32)idx_end,
        .parent = root, // Will be backpatched.
    };
    PG_ASSERT(title.pos_end - title.pos_start == s.len);
    *PG_DYN_PUSH_WITHIN_CAPACITY(&titles) = title;
    pos += (u64)(idx_start + idx_end);
  }

  for (u64 i = 0; i < titles.len; i++) {
    Title *title = PG_SLICE_AT_PTR(&titles, i);

    if (i > 0) {
      Title *previous = PG_SLICE_AT_PTR(&titles, i - 1);
      i8 level_diff = previous->level - title->level;

      if (level_diff > 0) {
        // The current title is a (great-)uncle of the current title.

        for (u64 _j = 0; _j < (u64)level_diff; _j++) {
          PG_ASSERT(title->parent);
          title->parent = title->parent->parent;
        }
      } else if (level_diff < 0) {
        // Check that we do not skip levels e.g. prevent `## Foo\n#### Bar\n`
        PG_ASSERT(level_diff == -1);
        title->parent = previous;
      } else if (0 == level_diff) { // Sibling.
        title->parent = previous->parent;
      }
    }
    PG_ASSERT(title->parent->level + 1 == title->level);
    Title *child = title->parent->first_child;

    // Add the node as last child of the parent.
    while (child && child->next_sibling) {
      child = child->next_sibling;
    }
    // Already one child present.
    if (child) {
      child->next_sibling = title;
    } else { // First child.
      title->parent->first_child = title;
    }
  }

  // Backpatch `id` field which is a hash of the full path to this node
  // including ancestors.
  for (u64 i = 0; i < titles.len; i++) {
    Title *title = PG_SLICE_AT_PTR(&titles, i);
    title->hash = title_compute_hash(title, FNV_SEED);
  }

  PG_ASSERT(nullptr == root->next_sibling);

  return root;
}

static void title_print(Title *title) {
  if (!title) {
    return;
  }
  PG_ASSERT(title->level > 0);

  for (i64 i = 0; i < title->level - 2; i++) {
    printf("  ");
  }
  if (1 == title->level) {
    printf(".\n");
  } else {
    printf("title='%.*s' id=%u\n", (int)title->title.len, title->title.data,
           title->hash);
  }
  title_print(title->first_child);
  title_print(title->next_sibling);
}

static void html_write_decorated_titles_rec(PgString html, Pgu8Dyn *sb,
                                            Title *title,
                                            u64 *last_title_pos_end,
                                            PgAllocator *allocator) {
  if (!title) {
    return;
  }
  PG_ASSERT(title->pos_end > title->pos_start);

  PG_DYN_APPEND_SLICE(
      sb, PG_SLICE_RANGE(html, *last_title_pos_end, title->pos_start),
      allocator);
  if (*last_title_pos_end != 0) {
    PG_ASSERT(*last_title_pos_end < title->pos_end);
  }
  *last_title_pos_end = title->pos_end;

  PG_DYN_APPEND_SLICE(sb, PG_S("<h"), allocator);
  pg_string_builder_append_u64_as_string(sb, title->level, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S(" id=\""), allocator);
  pg_string_builder_append_u64_as_string(sb, title->hash, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("\">\n  <a class=\"title\" href=\"#"),
                      allocator);
  pg_string_builder_append_u64_as_string(sb, title->hash, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("\">"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->title, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</a>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("  <a class=\"hash-anchor\" href=\"#"),
                      allocator);
  pg_string_builder_append_u64_as_string(sb, title->hash, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
  PG_DYN_APPEND_SLICE(
      sb,
      PG_S("\" aria-hidden=\"true\" "
           "onclick=\"navigator.clipboard.writeText(this.href);\"></a>\n"),
      allocator);

  html_write_decorated_titles_rec(html, sb, title->first_child,
                                  last_title_pos_end, allocator);
  html_write_decorated_titles_rec(html, sb, title->next_sibling,
                                  last_title_pos_end, allocator);
}

static void html_write_decorated_titles(PgString html, Pgu8Dyn *sb, Title *root,
                                        PgAllocator *allocator) {
  PG_ASSERT(nullptr == root->next_sibling);

  // No titles, noop.
  if (!root->first_child) {
    PG_DYN_APPEND_SLICE(sb, html, allocator);
    return;
  }
  u64 last_title_pos_end = 0;

  html_write_decorated_titles_rec(html, sb, root->first_child,
                                  &last_title_pos_end, allocator);

  PG_DYN_APPEND_SLICE(sb, PG_SLICE_RANGE_START(html, last_title_pos_end),
                      allocator);
  PG_ASSERT(sb->len > html.len);
}

static void article_write_toc_rec(Pgu8Dyn *sb, Title *title,
                                  PgAllocator *allocator) {
  if (!title) {
    return;
  }

  if (title->level > 1) {
    PG_DYN_APPEND_SLICE(sb, PG_S("\n  <li>\n    <a class=\"title\" href=\"#"),
                        allocator);
    pg_string_builder_append_u64_as_string(sb, title->hash, allocator);
    PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
    PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
    PG_DYN_APPEND_SLICE(sb, PG_S("\">"), allocator);
    PG_DYN_APPEND_SLICE(sb, title->title, allocator);
    PG_DYN_APPEND_SLICE(sb, PG_S("</a>\n"), allocator);
  }

  if (title->first_child) {
    PG_DYN_APPEND_SLICE(sb, PG_S("<ul>\n"), allocator);
  }
  article_write_toc_rec(sb, title->first_child, allocator);
  if (title->first_child) {
    PG_DYN_APPEND_SLICE(sb, PG_S("</ul>\n"), allocator);
  }

  if (title->level > 1) {
    PG_DYN_APPEND_SLICE(sb, PG_S("  </li>\n"), allocator);
  }

  article_write_toc_rec(sb, title->next_sibling, allocator);
}

static void article_write_toc(Pgu8Dyn *sb, Title *root,
                              PgAllocator *allocator) {
  if (!root->first_child) {
    return;
  }

  PG_DYN_APPEND_SLICE(sb, PG_S(" <strong>Table of contents</strong>\n"),
                      allocator);
  article_write_toc_rec(sb, root, allocator);
}

static void article_generate_html_file(PgFileDescriptor markdown_file,
                                       u64 metadata_offset, Article *article,
                                       PgString header, PgString footer,
                                       PgAllocator *allocator) {

  PgString article_html =
      markdown_to_html(markdown_file, metadata_offset, allocator);
  // TODO: build search index on html.

  Title *title_root = html_parse_titles(article_html, allocator);
  title_print(title_root);

  Pgu8Dyn sb = {0};
  PG_DYN_ENSURE_CAP(&sb, 4096, allocator);
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
  PG_DYN_APPEND_SLICE(&sb, PG_S("</h1>\n"), allocator);

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

  article_write_toc(&sb, title_root, allocator);

  PG_DYN_APPEND_SLICE(&sb, PG_S("\n"), allocator);

  html_write_decorated_titles(article_html, &sb, title_root, allocator);

  PG_DYN_APPEND_SLICE(&sb, PG_S(BACK_LINK), allocator);
  PG_DYN_APPEND_SLICE(&sb, footer, allocator);
  PgString html = PG_DYN_SLICE(PgString, sb);
  PG_ASSERT(!pg_string_contains(html, PG_S("\n>\n")));
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

  PgFileDescriptorResult res_markdown_file =
      pg_file_open(git_stat.path_rel, PG_FILE_ACCESS_READ, false, allocator);
  PG_ASSERT(0 == res_markdown_file.err);
  PgFileDescriptor markdown_file = res_markdown_file.res;

  u8 tmp[1024] = {0};
  PgString markdown_header = {
      .data = tmp,
      .len = PG_STATIC_ARRAY_LEN(tmp),
  };
  PgU64Result res_markdown_header =
      pg_file_read(markdown_file, markdown_header);
  PG_ASSERT(0 == res_markdown_header.err);
  markdown_header.len = res_markdown_header.res;
  PG_ASSERT(markdown_header.len > 16);

  PgStringCut cut =
      pg_string_cut_string(markdown_header, PG_S(METADATA_DELIMITER));
  PG_ASSERT(cut.ok);
  PgString metadata_str = cut.left;
  PG_ASSERT(!pg_string_is_empty(metadata_str));
  u64 metadata_offset = cut.left.len + PG_STATIC_ARRAY_LEN(METADATA_DELIMITER);

  cut = pg_string_cut_byte(metadata_str, '\n');
  PG_ASSERT(cut.ok);
  PgString metadata_title = cut.left;
  PG_ASSERT(!pg_string_is_empty(metadata_title));
  PgString metadata_tags = cut.right;
  PG_ASSERT(!pg_string_is_empty(metadata_tags));

  cut = pg_string_cut_byte(metadata_title, ':');
  PG_ASSERT(cut.ok);
  article.title = pg_string_dup(pg_string_trim(cut.right, ' '), allocator);
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
      PgString tag = pg_string_trim_space(remaining);
      *PG_DYN_PUSH_WITHIN_CAPACITY(&tags) = pg_string_dup(tag, allocator);
      break;
    }

    PgString tag = pg_string_trim(cut.left, ' ');
    remaining = cut.right;

    *PG_DYN_PUSH_WITHIN_CAPACITY(&tags) = pg_string_dup(tag, allocator);
  }
  PG_ASSERT(tags.len > 0);
  article.tags = PG_DYN_SLICE(PgStringSlice, tags);

  PgString stem = pg_path_stem(git_stat.path_rel);
  article.html_file_name = pg_string_concat(stem, PG_S(".html"), allocator);

  article_generate_html_file(markdown_file, metadata_offset, &article, header,
                             footer, allocator);

  PG_ASSERT(0 == pg_file_close(markdown_file));
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

static void home_page_generate(ArticleSlice articles, PgString header,
                               PgString footer, PgAllocator *allocator) {

  qsort(articles.data, articles.len, sizeof(Article),
        article_cmp_by_creation_date_desc);

  PgString markdown_file_path = PG_S("index.md");
  PgString html_file_path = PG_S("index.html");

  Pgu8Dyn sb = pg_sb_make_with_cap(32 * PG_KiB, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<!DOCTYPE html>\n<html>\n<head>\n<title>"),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("Philippe Gaultier's blog"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</title>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, header, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("\n<div class=\"articles\">\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("  <h2 id=\"articles\">Articles</h2>\n"),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("  <ul>\n"), allocator);

  for (u64 i = 0; i < articles.len; i++) {
    Article a = PG_SLICE_AT(articles, i);

    if (pg_string_eq(a.html_file_name, PG_S("body_of_work.html"))) {
      continue;
    }
    PG_DYN_APPEND_SLICE(&sb, PG_S("\n  <li>\n"), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("    <div class=\"home-link\">\n"),
                        allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("      <span class=\"date\">"), allocator);
    PG_DYN_APPEND_SLICE(&sb, datetime_to_date(a.creation_date), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("</span>\n"), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("      <a href=\"/blog/"), allocator);
    PG_DYN_APPEND_SLICE(&sb, a.html_file_name, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("\">"), allocator);
    PG_DYN_APPEND_SLICE(&sb, a.title, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("</a>\n"), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("    </div>\n"), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("<div class=\"tags\">\n"), allocator);

    for (u64 j = 0; j < a.tags.len; j++) {
      PgString tag = PG_SLICE_AT(a.tags, j);
      PgString id = html_make_id(tag, allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S(" <a href=\"/blog/articles-by-tag.html#"),
                          allocator);
      PG_DYN_APPEND_SLICE(&sb, id, allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("\" class=\"tag\">"), allocator);
      PG_DYN_APPEND_SLICE(&sb, tag, allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("</a>"), allocator);
    }
    PG_DYN_APPEND_SLICE(&sb, PG_S("</div></li>"), allocator);
  }
  PG_DYN_APPEND_SLICE(&sb, PG_S("  </ul>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</div>\n"), allocator);

  PgFileDescriptorResult res_markdown_file =
      pg_file_open(markdown_file_path, PG_FILE_ACCESS_READ, false, allocator);
  PG_ASSERT(0 == res_markdown_file.err);
  PgString html = markdown_to_html(res_markdown_file.res, 0, allocator);

  Title *title_root = html_parse_titles(html, allocator);
  html_write_decorated_titles(html, &sb, title_root, allocator);

  PG_DYN_APPEND_SLICE(&sb, footer, allocator);

  PG_ASSERT(0 == pg_file_write_full(html_file_path, PG_DYN_SLICE(PgString, sb),
                                    allocator));
  PG_ASSERT(0 == pg_file_close(res_markdown_file.res));
}

#define HASH_TABLE_EXP 10

typedef struct {
  PgString keys[1 << HASH_TABLE_EXP];
  ArticleDyn values[1 << HASH_TABLE_EXP];
} ArticlesByTag;

static ArticleDyn *articles_by_tag_lookup(ArticlesByTag *table, PgString key) {
  u64 hash = pg_hash_fnv(key);
  u64 mask = (1 << HASH_TABLE_EXP) - 1;
  u32 step = (hash >> (64 - HASH_TABLE_EXP)) | 1;
  for (u64 i = hash;;) {
    i = (i + step) & mask;

    PgString *k =
        PG_C_ARRAY_AT_PTR(table->keys, PG_STATIC_ARRAY_LEN(table->keys), i);
    if (!pg_string_is_empty(key)) {
      *k = key;
      return table->values + i;
    } else if (pg_string_eq(*k, key)) {
      return table->values + i;
    }
  }
}

static void tags_page_generate(ArticleSlice articles, PgString header,
                               PgString footer, PgAllocator *allocator) {

  PgStringDyn tags_lexicographically_ordered = {0};
  PG_DYN_ENSURE_CAP(&tags_lexicographically_ordered, 128, allocator);

  ArticlesByTag articles_by_tag = {0};

  for (u64 i = 0; i < articles.len; i++) {
    Article article = PG_SLICE_AT(articles, i);

    for (u64 j = 0; j < article.tags.len; j++) {
      PgString tag = PG_SLICE_AT(article.tags, j);
      PG_ASSERT(!pg_string_is_empty(tag));

      *PG_DYN_PUSH(&tags_lexicographically_ordered, allocator) = tag;
      ArticleDyn *articles_for_tag =
          articles_by_tag_lookup(&articles_by_tag, tag);
      PG_DYN_ENSURE_CAP(articles_for_tag, 128, allocator);
      *PG_DYN_PUSH(articles_for_tag, allocator) = article;
    }
  }
  pg_sort_unique(tags_lexicographically_ordered.data, sizeof(PgString),
                 &tags_lexicographically_ordered.len, pg_string_cmp_qsort);

  Pgu8Dyn sb = pg_sb_make_with_cap(4096, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<!DOCTYPE html>\n<html>\n<head>\n<title>"),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("Articles by tag"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</title>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, header, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S(BACK_LINK), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<h1>Articles by tag</h1>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<ul>\n"), allocator);

  for (u64 i = 0; i < tags_lexicographically_ordered.len; i++) {
    PgString tag = PG_SLICE_AT(tags_lexicographically_ordered, i);

    PG_DYN_APPEND_SLICE(&sb, PG_S("<li id=\""), allocator);
    PG_DYN_APPEND_SLICE(&sb, html_make_id(tag, allocator), allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("\"><span class=\"tag\">"), allocator);
    PG_DYN_APPEND_SLICE(&sb, tag, allocator);
    PG_DYN_APPEND_SLICE(&sb, PG_S("</span><ul>\n"), allocator);

    // TODO: Articles.
    ArticleDyn *articles_for_tag =
        articles_by_tag_lookup(&articles_by_tag, tag);
    PG_ASSERT(articles_for_tag->len > 0);

    qsort(articles_for_tag->data, articles_for_tag->len, sizeof(Article),
          article_cmp_by_creation_date_asc);

    for (u64 j = 0; j < articles_for_tag->len; j++) {
      Article article = PG_SLICE_AT(*articles_for_tag, j);

      PG_DYN_APPEND_SLICE(&sb, PG_S("<li>\n"), allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("  <span class=\"date\">"), allocator);
      PG_DYN_APPEND_SLICE(&sb, datetime_to_date(article.creation_date),
                          allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("</span>\n"), allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("  <a href=\""), allocator);
      PG_DYN_APPEND_SLICE(&sb, article.html_file_name, allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("\">"), allocator);
      PG_DYN_APPEND_SLICE(&sb, article.title, allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("</a>\n"), allocator);
      PG_DYN_APPEND_SLICE(&sb, PG_S("</li>\n"), allocator);
    }

    PG_DYN_APPEND_SLICE(&sb, PG_S("</ul></li>\n"), allocator);
  }

  PG_DYN_APPEND_SLICE(&sb, PG_S("</ul>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, footer, allocator);

  PgString html_file_name = PG_S("articles-by-tag.html");
  PG_ASSERT(0 == pg_file_write_full(html_file_name, PG_DYN_SLICE(PgString, sb),
                                    allocator));
}

static void rss_generate(ArticleSlice articles, PgAllocator *allocator) {
  qsort(articles.data, articles.len, sizeof(Article),
        article_cmp_by_creation_date_asc);

  Pgu8Dyn sb = pg_sb_make_with_cap(8 * PG_KiB, allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"),
                      allocator);
  PG_DYN_APPEND_SLICE(
      &sb, PG_S("<feed xmlns=\"http://www.w3.org/2005/Atom\">\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<title>Philippe Gaultier's blog</title>\n"),
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<link href=\""), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S(BASE_URL), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("\"/>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<updated>"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_SLICE_LAST(articles).modification_date,
                      allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</updated>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<author>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<name>Philippe Gaultier</name>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</author>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("<id>urn:uuid:"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S(FEED_UUID), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</id>\n"), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</feed>"), allocator);

  PG_ASSERT(0 == pg_file_write_full(PG_S("feed.xml"),
                                    PG_DYN_SLICE(PgString, sb), allocator));
}

int main() {
#if 0
  PgHeapAllocator heap_allocator = pg_make_heap_allocator();
  PgAllocator *allocator = pg_heap_allocator_as_allocator(&heap_allocator);
#endif

  PgArena arena = pg_arena_make_from_virtual_mem(100 * PG_MiB);
  PgArenaAllocator arena_allocator = pg_make_arena_allocator(&arena);
  PgAllocator *allocator = pg_arena_allocator_as_allocator(&arena_allocator);

  // TODO: Use `pg_copy_file` for header, footer?
  PgStringResult res_header =
      pg_file_read_full_from_path(PG_S("header.html"), allocator);
  PG_ASSERT(0 == res_header.err);
  PgString header = res_header.res;

  PgStringResult res_footer =
      pg_file_read_full_from_path(PG_S("footer.html"), allocator);
  PG_ASSERT(0 == res_footer.err);
  PgString footer = res_footer.res;

  ArticleSlice articles = articles_generate(header, footer, allocator);
  home_page_generate(articles, header, footer, allocator);
  tags_page_generate(articles, header, footer, allocator);
  rss_generate(articles, allocator);

  printf("generated %" PRIu64 " articles (arena use=%" PRIu64 "\n",
         articles.len, pg_arena_mem_use(arena));
}
