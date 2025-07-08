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
  + some less than optimal encodings are forced to avoid accidentally using RIP relative addressing, e.g. `lea rax, [r13]` gets encoded as `lea rax, [r13 + 0]`
- [ ] How to get the current SQL schema when all you have is lots of migrations (deltas)
- [ ] 'About' page
- [ ] Search and replace fish function
- [ ] Go+Dtrace: Tips
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

    ```shell
$ sudo dtrace -x flowindent -n 'pid$target::*createBrowserRecoveryFlow*:entry {this->trace=1;} pid$target::*selfservice*:entry,pid$target::*selfservice*:return /this->trace/ {} pid$target::*createBrowserRecoveryFlow*:return {this->trace=0;} ' -p $(pgrep -a kratos)
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
  this->body_len = uregs[1];
  this->body_ptr = (uint8_t*)uregs[0];

  this->s = copyinstr((user_addr_t)this->body_ptr, this->body_len);
  printf("Code: %s\n", this->s);
}

pid$target::github.com?ory?kratos*SendRecoveryCodeTo:entry {
  this->body_ptr = (uint8_t*)uregs[R_R4];
  this->body_len = uregs[R_R3];

  this->s = copyinstr((user_addr_t)this->body_ptr, this->body_len);
  printf("Body: %s\n", this->s);
}


pid$target::github.com?ory?kratos*GetRecoveryFlow:return {
  this->flow = (struct flow*)copyin(uregs[0],sizeof(struct flow));

  this->state= copyinstr((user_addr_t)this->flow->state_ptr, this->flow->state_len );
  trace(this->state);

  if (this->flow->payload_ptr){
    this->payload= copyinstr((user_addr_t)this->flow->payload_ptr, this->flow->payload_len );
    trace(this->payload);
  }

  ustack(10);
}

pid$target::github.com?ory?kratos*UpdateRecoveryFlow:entry {
  this->flow = (struct flow*)copyin(uregs[R_R3],sizeof(struct flow));

  this->state= copyinstr((user_addr_t)this->flow->state_ptr, this->flow->state_len );
  trace(this->state);


  if (this->flow->payload_ptr){
    this->payload= copyinstr((user_addr_t)this->flow->payload_ptr, this->flow->payload_len );
    trace(this->payload);
  }


  ustack(10);
}

pid$target::github.com?ory?kratos*CreateRecoveryFlow:entry {
  this->flow = (struct flow*)copyin(uregs[R_R3],sizeof(struct flow));

  this->state= copyinstr((user_addr_t)this->flow->state_ptr, this->flow->state_len );
  trace(this->state);

  if (this->flow->payload_ptr){
    this->payload= copyinstr((user_addr_t)this->flow->payload_ptr, this->flow->payload_len );
    trace(this->payload);
  }


  ustack(10);
}



## Blog implementation

- [ ] Dtrace syntax highlighting
- [ ] Articles excerpt on the home page?
- [ ] Browser live reload: 
  + Depends on: custom HTTP server, builtin file watch.
  + HTTP server serves and watches files for changes.
  + HTTP server injects a JS snippet when serving HTML files which listens for SSE events on a separate endpoint (e.g. `/live-reload`).
  + When a client sends a request to the server on `/live-reload` (i.e. subscribes), the server adds it the list of clients (of 1).
  + When a file changes on disk, the server sends a SSE to all registered clients.
  + The client reloads the page when a SSE event is received.
- [ ] Built-in file watch
- [ ] Built-in http server
- [ ] Dark mode
- [ ] Highlight only the changed lines in a code snippet
- [ ] Link to related articles at the end (requires post-processing after all articles have been generated)
- [ ] Syntax highlighting done statically
- [x] Add autogenerated html comment in rendered html files
- [x] Consider post-processing HTML instead of markdown to simplify e.g. to add title ids
- [x] Copy-paste button for code snippets
- [x] Search
- [x] Support markdown syntax in article title in metadata
- [x] Use libcmark to simplify parsing
- [x] Wrap line numbers in their own div
