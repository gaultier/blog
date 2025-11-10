## Ideas for articles

- [ ] sql differences between databases
- [ ] HTTP server using arenas
- [ ] ~~Migration from mbedtls to aws-lc~~
- [ ] ~~ASM crypto~~
- [ ] How this blog is made. Line numbers in code snippets.
- [ ] Banjo chords
- [ ] Dtrace VM
- [ ] From BIOS to bootloader to kernel to userspace
- [ ] A faster HTTP parser than NodeJS's one
- [ ] SHA1 multi-block hash
- [ ] How CGO calls are implemented in assembly
- [ ] How I develop Windows applications from the comfort of Linux
- [ ] ~~Add a JS/Wasm live running version of the programs described in each article. As a fallback: Video/Gif.
    + [ ] Kahn's algorithm~~
- [ ] Blog search implementation
- [ ] Weird and surprising things about x64 assembly
  + non symetric mnemonics (`cmp 1, rax` vs `cmp rax, 1`)
  + some different mnemonics encode to the same bytes (`jne`, `jz`)
  + diffent calling convention for functions & system calls in the SysV ABI (4th argument)
  + no (to my knowledge) mnemonic accepts 2 immediates or effective addresses as operands  e.g. `cmp 1, 0`
  + some less than optimal encodings are forced to avoid accidentally using RIP relative addressing, e.g. `lea rax, [r13]` gets encoded as `lea rax, [r13 + 0]`
- [ ] How to get the current SQL schema when all you have is lots of migrations (deltas)
- [ ] Search and replace fish function
- [ ] How DTrace works
      + mmap `/dev/dtrace`
      + Load `.d` files with definitions
      + Use `ioctl` on `/dev/dtrace`'s fd with commands (`DTRACEIOC_GO`, `DTRACEIOC_STOP`) and optionally data (e.g. DOF)
      + DOF is a bit like ELF: contains metadata about the machine etc, several sections, including one for the DIF (bytecode for the in-kernel DTrace VM). It's a lot like a Java .class file. Interestingly DOF supports sections for comments and source code?
      + Probes are enabled with `ioctl` commands
      + `ioctl` is both a command and also gets its (optional) 3rd argument filled by the kernel.

## Blog implementation

- [ ] browser: search shows the full title path to the match e.g. 'my_article: foo/bar/baz'
- [ ] browser: search highlights matched terms (and jumps to them with a link?)
- [ ] gen: Work mainly on markdown with libcmark instead of html for simplicity
- [ ] gen: Articles excerpt on the home page?
- [ ] gen: Browser live reload: 
  + Depends on: custom HTTP server, builtin file watch.
  + HTTP server serves and watches files for changes.
  + HTTP server injects a JS snippet when serving HTML files which listens for SSE events on a separate endpoint (e.g. `/live-reload`).
  + When a client sends a request to the server on `/live-reload` (i.e. subscribes), the server adds it the list of clients (of 1).
  + When a file changes on disk, the server sends a SSE to all registered clients.
  + The client reloads the page when a SSE event is received.
- [ ] gen: Built-in file watch
- [ ] gen: Built-in http server
- [ ] browser: Dark mode
- [ ] gen: Highlight only the changed lines in a code snippet
- [ ] gen: Link to related articles at the end (requires post-processing after all articles have been generated)
- [ ] gen: Syntax highlighting done statically
- [ ] std: debug allocator using the chrome trace format
