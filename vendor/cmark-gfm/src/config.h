#ifndef CMARK_CONFIG_H
#define CMARK_CONFIG_H

// Written by hand for the compilers `build.rs` uses (clang, gcc), in place of
// the one CMake generates from `config.h.in`.

#include <stdbool.h>

#define HAVE_STDBOOL_H
#define HAVE___BUILTIN_EXPECT
#define HAVE___ATTRIBUTE__
#define CMARK_ATTRIBUTE(list) __attribute__ (list)

#ifndef CMARK_INLINE
#define CMARK_INLINE inline
#endif

#endif
