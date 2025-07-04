.POSIX:
.SUFFIXES:

LD = lld

CFLAGS = -fpie -fno-omit-frame-pointer -gsplit-dwarf -march=native -fuse-ld=$(LD) -std=c23 -Wall -Wextra -Werror -g3

LDFLAGS = -flto

CC = clang

C_FILES = main.c submodules/cstd/lib.c $(wildcard *.h)

SANITIZERS = address,undefined

main_debug.bin: $(C_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@

main_debug_sanitizer.bin: $(C_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -fsanitize=$(SANITIZERS)

main_release.bin: $(C_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -O2 -flto

main_release_sanitizer.bin: $(C_FILES)
	$(CC) $(CFLAGS) $(LDFLAGS) main.c -o $@ -O2 -flto -fsanitize=$(SANITIZERS)

.PHONY: all
all: main_debug.bin main_debug_sanitizer.bin main_release.bin main_release_sanitizer.bin

.PHONY: gen
gen: main_release.bin
	./$<

.PHONY: clean
clean:
	rm *.o *.bin *.bin *.dwo || true

.PHONY: dev
dev: 
	ls *.{c,h,md} submodules/cstd/*.{c,h} | entr -c make gen
