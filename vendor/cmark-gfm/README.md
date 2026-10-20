# libcmark-gfm

The C markdown parser used by `src/main.rs`, built by `build.rs`.

Copied from https://github.com/github/cmark-gfm at commit
`27d942c8b0a62d192f616e5bf3578f4b6a89e180` (2026-09-28, version
0.29.0.gfm.13), keeping only the library sources and `COPYING`. It is our code
now, changed for the blog:

- Footnote references keep their label instead of being renumbered, and
  definitions stay where they are in the document instead of being moved to the
  end. The library's own HTML renderer, which the blog does not use, therefore
  no longer lays footnotes out like GitHub does.
- The arena allocator is thread-local, `cmark_arena_reset` keeps the first chunk
  for the next document, and `arena_realloc` grows the last allocation in place
  (and no longer overflows when shrinking). A thread's chunks are freed when it
  exits.
- The inline special-character tables (`SPECIAL_CHARS`, `SKIP_CHARS`) are
  thread-local: every parse edits them, so threads parsing at once raced.
- `cmark_node_get_literal_chunk` returns a literal without copying it.
- `config.h`, `cmark-gfm_export.h` and `cmark-gfm_version.h`, which CMake used
  to generate, are written by hand.

`src/scanners.c` and `extensions/ext_scanners.c` are generated from the `.re`
files next to them with re2c, and are committed.
