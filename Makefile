.POSIX:
.SUFFIXES:

LD = lld

CFLAGS = -fpie -fno-omit-frame-pointer -gsplit-dwarf -march=native -fuse-ld=$(LD) -std=c23 -Wall -Wextra -Werror -g3

LDFLAGS = -flto

CC = clang

C_FILES = main.c submodules/cstd/lib.c $(wildcard *.h)

SANITIZERS = address,undefined

.PHONY: gen
gen: main_release.bin
	./$<

main_debug.bin: $(C_FILES) submodules/cmark-gfm/build/src/cmark-gfm
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@

main_debug_sanitizer.bin: $(C_FILES) submodules/cmark-gfm/build/src/cmark-gfm
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -fsanitize=$(SANITIZERS)

main_release.bin: $(C_FILES) submodules/cmark-gfm/build/src/cmark-gfm
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -O2 -flto

main_release_sanitizer.bin: $(C_FILES) submodules/cmark-gfm/build/src/cmark-gfm
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -O2 -flto -fsanitize=$(SANITIZERS)

submodules/cmark-gfm/build/src/cmark-gfm: 
	make -C ./submodules/cmark

.PHONY: all
all: main_debug.bin main_debug_sanitizer.bin main_release.bin main_release_sanitizer.bin


.PHONY: clean
clean:
	rm *.o *.bin *.bin *.dwo || true

.PHONY: dev
dev: 
	ls *.{c,h,md} submodules/cstd/*.{c,h} header.html footer.html | entr -cnr make gen

# TODO: Consider moving all checks to `main.c`.
.PHONY: check
check:
# Catch incorrect `an` e.g. `an fox`.
	rg '\san\s+[bcdfgjklmnpqrstvwxyz]' -i -t markdown || true
# Catch incorrect `a` e.g. `a opening`.
	rg '\sa\s+[aei]' -i -t markdown || true
# Catch code blocks without explicit type.
	rg '^[ ]*```[ ]*\n\S' -t markdown --multiline --glob='!todo.md' || true
# Avoid mixing `KiB` and `Kib` - prefer the former.
	rg '\b[KMGT]ib\b' -t markdown || true
# Catch empty first line in code block.
	rg '^\s*```\w+\n\n' -t markdown --multiline || true
