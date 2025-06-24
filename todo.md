## Ideas for articles

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
- [ ] C++ web server undefined behavior bug
- [ ] Weird and surprising things about x64 assembly
  + non symetric mnemonics (`cmp 1, rax` vs `cmp rax, 1`)
  + some different mnemonics encode to the same bytes (`jne`, `jz`)
  + diffent calling convention for functions & system calls in the SysV ABI (4th argument)
  + no (to my knowledge) mnemonic accepts 2 immediates or effective addresses as operands  e.g. `cmp 1, 0`
- [ ] How to get the current SQL schema when all you have is lots of migrations (deltas)
- [ ] 'About' page
- [ ] Search and replace fish function
- [ ] Go+Dtrace: 'Go and Dtrace: Useful but clunky'
    ```
pid$target::*DispatchMessage:entry {
  stack_offset =656;
  this->data=copyin(uregs[R_SP] + stack_offset, 16);
  tracemem(this->data, 16);

  this->body_len = *((ssize_t*)this->data+1);

  this->body_ptr = (uint8_t**)this->data;

  this->s = copyinstr((user_addr_t)*this->body_ptr, this->body_len);
  printf("msg.body: %s\n", this->s);
}
    ```

    ```
$ sudo dtrace  -n 'pid$target::github.com?ory?kratos*SMSBody:return{  this->body_len = uregs[1]; this->body_ptr = (uint8_t*)uregs[0];

                                                                                this->s = copyinstr((user_addr_t)this->body_ptr, this->body_len);
                                                                                printf("msg.Body: %s\\n", this->s);
                                                                              }' -p $(pgrep -a kratos)
    ```


    ```

 10   2968 github.com/ory/kratos/courier.(*courier).DispatchMessage:entry 
             0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f  0123456789abcdef
         0: 80 ae c7 01 40 01 00 00 1e 00 00 00 00 00 00 00  ....@...........
msg.Body: Your recovery code is: 707144
    ```

    ```sh
$ sudo dtrace -x flowindent -n 'pid$target::*createBrowserRecoveryFlow*:entry {self->trace=1;} pid$target::*selfservice*:entry,pid$target::*selfservice*:return /self->trace/ {} pid$target::*createBrowserRecoveryFlow*:return {self->trace=0;} ' -p $(pgrep -a kratos)
dtrace: description 'pid$target::*createBrowserRecoveryFlow*:entry ' matched 3602 probes
CPU FUNCTION                                 
 10  -> github.com/ory/kratos/selfservice/flow/recovery.(*Handler).createBrowserRecoveryFlow-fm 
 10    -> github.com/ory/kratos/selfservice/flow/recovery.(*Handler).createBrowserRecoveryFlow 
 10      -> github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10      <- github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10      -> github.com/ory/kratos/selfservice/strategy/link.(*Strategy).RecoveryStrategyID 
 10      <- github.com/ory/kratos/selfservice/strategy/link.(*Strategy).RecoveryStrategyID 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.Strategies.Strategy 
 10        -> github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10        <- github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10        -> github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10        <- github.com/ory/kratos/selfservice/strategy/code.(*Strategy).RecoveryStrategyID 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.Strategies.Strategy 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.NewFlow 
 10        -> github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectUseSourceURL.func1 
 10        <- github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectUseSourceURL.func1 
 10        -> github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectAllowURLs.func2 
 10        <- github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectAllowURLs.func2 
 10        -> github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectAllowSelfServiceURLs.func3 
 10        <- github.com/ory/kratos/selfservice/flow/recovery.NewFlow.SecureRedirectAllowSelfServiceURLs.func3 
 10        -> github.com/ory/kratos/selfservice/flow.AppendFlowTo 
 10        <- github.com/ory/kratos/selfservice/flow.AppendFlowTo 
 10        -> github.com/ory/kratos/selfservice/strategy/code.(*Strategy).NodeGroup 
 10        <- github.com/ory/kratos/selfservice/strategy/code.(*Strategy).NodeGroup 
 10        -> github.com/ory/kratos/selfservice/strategy/code.(*Strategy).PopulateRecoveryMethod 
 10        <- github.com/ory/kratos/selfservice/strategy/code.(*Strategy).PopulateRecoveryMethod 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.NewFlow 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*HookExecutor).PreRecoveryHook 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*HookExecutor).PreRecoveryHook 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).TableName 
 10      -> github.com/ory/kratos/selfservice/flow.(*State).Value 
 10      <- github.com/ory/kratos/selfservice/flow.(*State).Value 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).AfterSave 
 10        -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).SetReturnTo 
 10        <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).SetReturnTo 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).AfterSave 
 10      -> github.com/ory/kratos/selfservice/flow/recovery.(*Flow).AppendTo 
 10      <- github.com/ory/kratos/selfservice/flow/recovery.(*Flow).AppendTo 
 10    <- github.com/ory/kratos/selfservice/flow/recovery.(*Handler).createBrowserRecoveryFlow 
    ```

    ```
$ sudo dtrace -n 'pid$target::*SendRecoveryCodeTo:entry {this->len = uregs[5]; this->ptr = uregs[4]; this->str = copyinstr(this->ptr, this->len); printf("Code: %s\n", this->str);} pid$target::*SendRecoveryCodeTo:return {trace(uregs[R_R0])}' -p $(pgrep -a kratos)
    ```

    ```
$ sudo dtrace -n 'struct flow {uint8_t pad1[136]; uint8_t* state_ptr; ssize_t state_len;}; pid$target::github.com?ory?kratos*GetRecoveryFlow:return {this->flow = (struct flow*)copyin(uregs[0],sizeof(struct flow)); this->state= copyinstr((user_addr_t)this->flow->state_ptr, this->flow->state_len ); trace(this->state);
                                                              }' -p $(pgrep -a kratos)
dtrace: description 'struct flow ' matched 2 probes
CPU     ID                    FUNCTION:NAME
 11  53391 github.com/ory/kratos/persistence/sql.(*Persister).GetRecoveryFlow:return   choose_method                    
    ```

    ```
struct flow {
  uint8_t pad1[136];

  uint8_t* state_ptr; 
  size_t state_len;

  uint8_t pad2[128];

  uint8_t* payload_ptr; 
  size_t payload_len;
};

pid$target::github.com?ory?kratos*GenerateCode:return {
  self->body_len = uregs[1];
  self->body_ptr = (uint8_t*)uregs[0];

  self->s = copyinstr((user_addr_t)self->body_ptr, self->body_len);
  printf("Code: %s\n", self->s);
}

pid$target::github.com?ory?kratos*SendRecoveryCodeTo:entry {
  self->body_ptr = (uint8_t*)uregs[R_R4];
  self->body_len = uregs[R_R3];

  self->s = copyinstr((user_addr_t)self->body_ptr, self->body_len);
  printf("Body: %s\n", self->s);
}


pid$target::github.com?ory?kratos*GetRecoveryFlow:return {
  self->flow = (struct flow*)copyin(uregs[0],sizeof(struct flow));

  self->state= copyinstr((user_addr_t)self->flow->state_ptr, self->flow->state_len );
  trace(self->state);

  if (self->flow->payload_ptr){
    self->payload= copyinstr((user_addr_t)self->flow->payload_ptr, self->flow->payload_len );
    trace(self->payload);
  }

  ustack(10);
}

pid$target::github.com?ory?kratos*UpdateRecoveryFlow:entry {
  self->flow = (struct flow*)copyin(uregs[R_R3],sizeof(struct flow));

  self->state= copyinstr((user_addr_t)self->flow->state_ptr, self->flow->state_len );
  trace(self->state);


  if (self->flow->payload_ptr){
    self->payload= copyinstr((user_addr_t)self->flow->payload_ptr, self->flow->payload_len );
    trace(self->payload);
  }


  ustack(10);
}

pid$target::github.com?ory?kratos*CreateRecoveryFlow:entry {
  self->flow = (struct flow*)copyin(uregs[R_R3],sizeof(struct flow));

  self->state= copyinstr((user_addr_t)self->flow->state_ptr, self->flow->state_len );
  trace(self->state);

  if (self->flow->payload_ptr){
    self->payload= copyinstr((user_addr_t)self->flow->payload_ptr, self->flow->payload_len );
    trace(self->payload);
  }


  ustack(10);
}



    ```

## Blog implementation

- [x] Add autogenerated html comment in rendered html files
- [x] Consider post-processing HTML instead of markdown to simplify e.g. to add title ids
- [x] Support markdown syntax in article title in metadata
- [x] Use libcmark to simplify parsing
- [ ] Link to related articles at the end (requires post-processing after all articles have been generated)
- [x] Search
  + May require proper markdown parsing (e.g. with libcmark) to avoid having html/markdown elements in search results, to get the (approximate) location of each results (e.g. parent title), and show a rendered excerpt in search results. Perhaps simply post-process html?
  + Results are shown inline
  + Results show (some) surrounding text
  + Results have a link to the page and surrounding html section
  + Implementation: trigrams in js/wasm? Fuzzy search? See https://pkg.odin-lang.org/search.js
  + Search code/data are only fetched lazily
  + Cache invalidation of the search blob e.g. when an article is created/modified? => split search code (rarely changes) and data (changes a lot)
  + Code snippets included in search results?
- [ ] Articles excerpt on the home page?
- [ ] Dark mode
- [ ] Browser live reload
  + Send the F5 key to the browser window (does not work in Wayland)
  + Somehow send an IPC message to the right browser process to reload the page?
  + HTTP server & HTML page communicate via websocket to reload content (complex)
  + HTTP2 force push to force the browser to get the new page version?
  + Server sent event to reload the page
  + Websocket
- [ ] Built-in http server
- [ ] Built-in file watch
- [ ] Syntax highlighting done statically
- [ ] Highlight only the changed lines in a code snippet
- [ ] Copy-paste button for code snippets
