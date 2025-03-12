#include "./submodules/cstd/lib.c"

// Plan:
// - Reader header & footer
// - Get all markdown files from git along with their creation/modification date
// - For each markdown file:
//   + Read metadata at the beginning of the file
//   + Parse metadata
//   + Convert markdown to HTML
//   + Parse titles from HTML
//   + Generate TOC
//   + Decorate titles with HTML id for links
//   + Generate final HTML with header, TOC, decorated content, and footer.
// - Generate tags page
// - Generate home page
// - Generate RSS feed

#define FEED_UUID                                                              \
  ((PgUuid){                                                                   \
      .value = {0x9c, 0x06, 0x5c, 0x53, 0x31, 0xbc, 0x40, 0x49, 0xa7, 0x95,    \
                0x93, 0x68, 0x02, 0xa6, 0xb1, 0xdf},                           \
      .version = 5,                                                            \
  })
#define BASE_URL "https://gaultier.github.io/blog"
#define METADATA_DELIMITER "---"
#define BACK_LINK "<p><a href=\"/blog\"> ⏴ Back to all articles</a></p>\n"
#define FNV_SEED ((u32)0x811c9dc5)

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
      PgStringCut cut = pg_string_cut_rune(remaining, '\n');
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
      PgStringCut cut = pg_string_cut_rune(remaining, '\n');
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

      PgStringCut cut = pg_string_cut_rune(remaining, '\n');
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
        cut = pg_string_cut_rune(line, '\t');
        PG_ASSERT(cut.ok);
        line = cut.right;

        cut = pg_string_cut_rune(line, '\t');
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

    if (pg_rune_is_alphanumeric(c)) {
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
  PgStringCut cut = pg_string_cut_rune(datetime, 'T');
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

static void html_collect_titles_rec(PgHtmlNode *node, Title *parent,
                                    PgAllocator *allocator) {
  PG_ASSERT(parent);

  if (!node) {
    return;
  }

  u8 level = pg_html_get_title_level(node);
  if (0 == level) {
    html_collect_titles_rec(node->first_child, parent, allocator);
    html_collect_titles_rec(node->next_sibling, parent, allocator);
    return;
  }

  Title *new_title = pg_alloc(allocator, sizeof(Title), _Alignof(Title), 1);
  new_title->level = level;
  new_title->parent = parent;
  new_title->title = pg_html_get_title_content(node);
  new_title->content_html_id = html_make_id(new_title->title, allocator);
  new_title->hash = title_compute_hash(new_title, FNV_SEED);
  new_title->pos_start = node->token_start.start;
  new_title->pos_end = node->token_start.end;

  if (!parent->first_child) {
    parent->first_child = new_title;
  } else {
    Title *it = parent->first_child;
    while (it->next_sibling) {
      it = it->next_sibling;
    }
    it->next_sibling = new_title;
  }
  html_collect_titles_rec(node->first_child, new_title, allocator);
  html_collect_titles_rec(node->next_sibling, parent, allocator);
}

[[nodiscard]]
static Title *html_collect_titles(PgHtmlNode *html_root,
                                  PgAllocator *allocator) {
  Title *title_root = pg_alloc(allocator, sizeof(Title), _Alignof(Title), 1);
  title_root->level = 1;
  title_root->parent = title_root;

  html_collect_titles_rec(html_root, title_root, allocator);

  return title_root;
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

  PG_DYN_APPEND_SLICE(sb, PG_S("h"), allocator);
  pg_string_builder_append_u64(sb, title->level, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S(" id=\""), allocator);
  pg_string_builder_append_u64(sb, title->hash, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("\">\n  <a class=\"title\" href=\"#"),
                      allocator);
  pg_string_builder_append_u64(sb, title->hash, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("-"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->content_html_id, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("\">"), allocator);
  PG_DYN_APPEND_SLICE(sb, title->title, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</a>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("  <a class=\"hash-anchor\" href=\"#"),
                      allocator);
  pg_string_builder_append_u64(sb, title->hash, allocator);
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
    PG_DYN_APPEND_SLICE(sb, PG_S("\n  <li>\n    <a href=\"#"), allocator);
    pg_string_builder_append_u64(sb, title->hash, allocator);
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

static void html_node_print(PgHtmlNode *node, u64 depth) {
  if (!node) {
    return;
  }
  for (u64 i = 0; i < depth; i++) {
    printf("  ");
  }

  switch (node->token_start.kind) {
  case PG_HTML_TOKEN_KIND_TEXT:
    printf("text: %.*s\n", (int)node->token_start.text.len,
           node->token_start.text.data);
    break;
  case PG_HTML_TOKEN_KIND_TAG_OPENING:
    printf("open: %.*s\n", (int)node->token_start.tag.len,
           node->token_start.tag.data);
    html_node_print(node->first_child, depth + 2);
    for (u64 i = 0; i < depth; i++) {
      printf("  ");
    }
    printf("close: %.*s\n", (int)node->token_end.tag.len,
           node->token_end.tag.data);
    html_node_print(node->next_sibling, depth);
    break;
  case PG_HTML_TOKEN_KIND_NONE: // Root.
    html_node_print(node->first_child, depth + 2);
    break;
  case PG_HTML_TOKEN_KIND_TAG_CLOSING:
  case PG_HTML_TOKEN_KIND_ATTRIBUTE:
  case PG_HTML_TOKEN_KIND_COMMENT:
  case PG_HTML_TOKEN_KIND_DOCTYPE:
  default:
    PG_ASSERT(0);
  }
}

static void article_generate_html_file(PgFileDescriptor markdown_file,
                                       u64 metadata_offset, Article *article,
                                       PgString header, PgString footer,
                                       PgAllocator *allocator) {

  PgString article_html =
      markdown_to_html(markdown_file, metadata_offset, allocator);
  PgHtmlNodePtrResult res_parse = pg_html_parse(article_html, allocator);
  PG_ASSERT(0 == res_parse.err);

  PgHtmlNode *html_root = res_parse.res;
  html_node_print(html_root, 0);

  // TODO: build search index on html.

  Title *title_root = html_collect_titles(html_root, allocator);
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

  cut = pg_string_cut_rune(metadata_str, '\n');
  PG_ASSERT(cut.ok);
  PgString metadata_title = cut.left;
  PG_ASSERT(!pg_string_is_empty(metadata_title));
  PgString metadata_tags = cut.right;
  PG_ASSERT(!pg_string_is_empty(metadata_tags));

  cut = pg_string_cut_rune(metadata_title, ':');
  PG_ASSERT(cut.ok);
  article.title = pg_string_clone(pg_string_trim(cut.right, ' '), allocator);
  PG_ASSERT(!pg_string_is_empty(article.title));

  cut = pg_string_cut_rune(metadata_tags, ':');
  PG_ASSERT(cut.ok);
  PgString tags_str = cut.right;
  PG_ASSERT(!pg_string_is_empty(tags_str));

  PgString remaining = tags_str;
  PgStringDyn tags = {0};
  PG_DYN_ENSURE_CAP(&tags, 32, allocator);
  for (;;) {
    cut = pg_string_cut_rune(remaining, ',');
    if (!cut.ok) {
      PgString tag = pg_string_trim_space(remaining);
      *PG_DYN_PUSH_WITHIN_CAPACITY(&tags) = pg_string_clone(tag, allocator);
      break;
    }

    PgString tag = pg_string_trim(cut.left, ' ');
    remaining = cut.right;

    *PG_DYN_PUSH_WITHIN_CAPACITY(&tags) = pg_string_clone(tag, allocator);
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

  PgHtmlNodePtrResult res_parse = pg_html_parse(html, allocator);
  PG_ASSERT(0 == res_parse.err);

  PgHtmlNode *html_root = res_parse.res;
  html_node_print(html_root, 0);

  Title *title_root = html_collect_titles(html_root, allocator);
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

[[nodiscard]]
static ArticleDyn *articles_by_tag_lookup(ArticlesByTag *table, PgString key) {
  u64 hash = pg_hash_fnv(key);
  u32 mask = (1 << HASH_TABLE_EXP) - 1;
  u32 step = (hash >> (64 - HASH_TABLE_EXP)) | 1;
  for (u32 i = (u32)hash;;) {
    i = ((i + step) & mask);

    PgString *k =
        PG_C_ARRAY_AT_PTR(table->keys, PG_STATIC_ARRAY_LEN(table->keys), i);
    if (pg_string_is_empty(*k)) {
      *k = key;
      return table->values + i;
    } else if (pg_string_eq(*k, key)) {
      return table->values + i;
    }
  }
}

[[nodiscard]]
static PgStringSlice articles_by_tag_get_keys(ArticlesByTag table,
                                              PgAllocator *allocator) {
  PgStringDyn res = {0};

  for (u64 i = 0; i < PG_STATIC_ARRAY_LEN(table.keys); i++) {
    PgString key =
        PG_C_ARRAY_AT(table.keys, PG_STATIC_ARRAY_LEN(table.keys), i);
    if (pg_string_is_empty(key)) {
      continue;
    }

    *PG_DYN_PUSH(&res, allocator) = key;
  }

  qsort(res.data, res.len, sizeof(PgString), pg_string_cmp_qsort);

  return PG_DYN_SLICE(PgStringSlice, res);
}

static void tags_page_generate(ArticleSlice articles, PgString header,
                               PgString footer, PgAllocator *allocator) {

  ArticlesByTag articles_by_tag = {0};

  for (u64 i = 0; i < articles.len; i++) {
    Article article = PG_SLICE_AT(articles, i);

    for (u64 j = 0; j < article.tags.len; j++) {
      PgString tag = PG_SLICE_AT(article.tags, j);
      PG_ASSERT(!pg_string_is_empty(tag));

      ArticleDyn *articles_for_tag =
          articles_by_tag_lookup(&articles_by_tag, tag);
      PG_DYN_ENSURE_CAP(articles_for_tag, 128, allocator);
      *PG_DYN_PUSH(articles_for_tag, allocator) = article;
    }
  }
  PgStringSlice tags_lexicographically_ordered =
      articles_by_tag_get_keys(articles_by_tag, allocator);

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

    ArticleDyn *articles_for_tag =
        articles_by_tag_lookup(&articles_by_tag, tag);
    PG_ASSERT(articles_for_tag->len > 0);

    qsort(articles_for_tag->data, articles_for_tag->len, sizeof(Article),
          article_cmp_by_creation_date_asc);

    for (u64 j = 0; j < articles_for_tag->len; j++) {
      Article article = PG_SLICE_AT(*articles_for_tag, j);

      bool tag_found = false;
      for (u64 k = 0; k < article.tags.len; k++) {
        if (pg_string_eq(PG_SLICE_AT(article.tags, k), tag)) {
          tag_found = true;
          break;
        }
      }
      PG_ASSERT(tag_found);

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

static void article_rss_generate(Pgu8Dyn *sb, Article a,
                                 PgAllocator *allocator) {
  PgUuid article_uuid = pg_uuid_v5(FEED_UUID, a.html_file_name);

  PG_DYN_APPEND_SLICE(sb, PG_S("\n<entry>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("<title>"), allocator);
  PG_DYN_APPEND_SLICE(sb, pg_html_sanitize(a.title, allocator), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</title>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("<link href=\""), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S(BASE_URL), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("/"), allocator);
  PG_DYN_APPEND_SLICE(sb, a.html_file_name, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("\"/>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("<id>urn:uuid:"), allocator);
  PG_DYN_APPEND_SLICE(sb, pg_uuid_to_string(article_uuid, allocator),
                      allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</id>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("<updated>"), allocator);
  PG_DYN_APPEND_SLICE(sb, a.modification_date, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</updated>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("<published>"), allocator);
  PG_DYN_APPEND_SLICE(sb, a.creation_date, allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</published>\n"), allocator);
  PG_DYN_APPEND_SLICE(sb, PG_S("</entry>\n"), allocator);
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
  PG_DYN_APPEND_SLICE(&sb, pg_uuid_to_string(FEED_UUID, allocator), allocator);
  PG_DYN_APPEND_SLICE(&sb, PG_S("</id>\n"), allocator);

  for (u64 i = 0; i < articles.len; i++) {
    Article a = PG_SLICE_AT(articles, i);
    article_rss_generate(&sb, a, allocator);
  }

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

  printf("generated %" PRIu64 " articles (arena use=%" PRIu64 ")\n",
         articles.len, pg_arena_mem_use(arena));
}
