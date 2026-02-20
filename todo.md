## Ideas for articles

- [ ] Catch data races with DTrace?
- [ ] a weird advantage of using TLA+ (using TLA+ generated traces as input for the real program)
- [ ] compiler architecture and implementation with live playground
- [ ] For a few data races more
    + `ctx` accidental reassignement
    + mutating cache entry
    + using `*testing.T` from multiple goroutines.
    + adding synchronization does not fix it: e.g. `pop.SetNowFunc()`
    + custom concurrent map implementation vs sync.Map
    + using random number generator concurrently
    + have to document invariants in comments instead of in the type system
- [ ] A physical simulation of the transverse flute
- [ ] How DWARF works/Solving an AOC problem with DWARF (VM)
- [ ] How register allocation works (with visualization)
- [ ] sql differences between databases - porting an application to MySQL/PostgreSQL/SQLite
- [ ] HTTP server using arenas
- [ ] How this blog is made. Line numbers in code snippets.
- [ ] Banjo chords
- [ ] Dtrace VM
- [ ] From BIOS to bootloader to kernel to userspace
- [ ] A faster HTTP parser than NodeJS's one
- [ ] SHA1 multi-block hash
- [ ] How CGO calls are implemented in assembly
- [ ] Blog search implementation
- [ ] Weird and surprising things about x64 assembly
  + non symetric mnemonics (`cmp 1, rax` vs `cmp rax, 1`)
  + some different mnemonics encode to the same bytes (`jne`, `jz`)
  + diffent calling convention for functions & system calls in the SysV ABI (4th argument)
  + no (to my knowledge) mnemonic accepts 2 immediates or effective addresses as operands  e.g. `cmp 1, 0`
  + some less than optimal encodings are forced to avoid accidentally using RIP relative addressing, e.g. `lea rax, [r13]` gets encoded as `lea rax, [r13 + 0]`
  + Some operands size are small and force using a move-to-register e.g. `add rax, 1<<32` is illegal, need `mov rbx, 1<<32; add rax, rbx`
- [ ] Implement DTrace from scratch, from first principles
      + mmap `/dev/dtrace`
      + Load `.d` files with definitions
      + Use `ioctl` on `/dev/dtrace`'s fd with commands (`DTRACEIOC_GO`, `DTRACEIOC_STOP`) and optionally data (e.g. DOF)
      + DOF is a bit like ELF: contains metadata about the machine etc, several sections, including one for the DIF (bytecode for the in-kernel DTrace VM). It's a lot like a Java .class file. Interestingly DOF supports sections for comments and source code?
      + Probes are enabled with `ioctl` commands
      + `ioctl` is both a command and also gets its (optional) 3rd argument filled by the kernel.

## Blog implementation

- [ ] gen: link checker
- [ ] browser: search with `/` shortcut and appears on the side
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
- [ ] gen: Link to related articles at the end (requires post-processing after all articles have been generated)
- [ ] gen: Syntax highlighting done statically
