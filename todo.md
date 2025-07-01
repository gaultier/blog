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



    ```

    Popx investigation:

    Print the length of the slice being sorted (before fix):
    ```
pid$target:code.test.before:*NewMigrationBox:entry { self->t = 1}

pid$target:code.test.before:*NewMigrationBox:return { exit(0) }

pid$target:code.test.before:sort*: /self->t != 0/ {}
    ```

    ```
CPU     ID                    FUNCTION:NAME
  5  52085       sort.(*reverse).Len:return 1

  5  52085       sort.(*reverse).Len:return 2

  5  52085       sort.(*reverse).Len:return 3

  5  52085       sort.(*reverse).Len:return 4

  5  52085       sort.(*reverse).Len:return 5
  [...]

 11  52085       sort.(*reverse).Len:return 1690

 11  52085       sort.(*reverse).Len:return 1691

 11  52085       sort.(*reverse).Len:return 1692

 11  52085       sort.(*reverse).Len:return 1693
    ```

    Print the length of the slice being sorted (after fix):
    ```
pid$target::*SortFunc*Migration*:entry {
  printf("len=%d\n", uregs[R_R2]);
  ustack(40);
}

    ```

    Time:

    ```
pid$target::*popx?findMigrations:entry { self->t=timestamp } 

pid$target::*popx?findMigrations:return {
  printf("findMigrations:%d\n", (timestamp - self->t)/1000000)
}
```
```
sudo dtrace -n 'pid$target::*popx?findMigrations:entry {self->t=timestamp} pid$target::*popx?findMigrations:return /self->t!=0/ {printf("findMigrations:%d\n", (timestamp - self->t)/1000000)}' -c './code.test -test.count=1' -o /tmp/time2.txt
    ```

    List:
    ```
 $ sudo dtrace -n 'pid$target:code.test.before:*ory*: ' -c ./code.test.before -l | grep NewMigrationBox
209591   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox return
209592   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox entry
209593   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 return
209594   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4 entry
209595   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4.deferwrap1 return
209596   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.(*MigrationBox).findMigrations.func4.deferwrap1 entry
209597   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2 return
209598   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2 entry
209599   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2.1 return
209600   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func2.1 entry
209601   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1 return
209602   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1 entry
209603   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1.1 return
209604   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.func1.1 entry
209605   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.ParameterizedMigrationContent.func3 return
209606   pid47446  code.test.before github.com/ory/x/popx.NewMigrationBox.ParameterizedMigrationContent.func3 entry
```


 Time output:

 ```

CPU     ID                    FUNCTION:NAME
  4   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:2111957174

 10   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:2111958100

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

 10   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

  4   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:179

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:179

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:14413

  6   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:16007

  8   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

  9   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:182

  6   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:2111979385

  8   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:181

  5   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

  4   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:180

 13   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:2115

 11   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:2111983464

 11   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:18076

  5   4446 github.com/ory/x/popx.NewMigrationBox:return NewMigrationBox:182


 ```


  See SQL opens:

  ```

syscall::open:entry { 
  filename = copyinstr(arg0);

  if (rindex(filename, ".sql") == strlen(filename)-4) {
    printf("%s\n", filename)
  }
} 
  ```

  ```sh
$ sudo dtrace -s ~/scratch/popx_opens.dtrace -c 'go test -tags=sqlite -c'

CPU     ID                    FUNCTION:NAME
 10    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.cockroach.down.sql

  5    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.postgres.down.sql

 10    178                       open:entry /Users/philippe.gaultier/company-code/x/networkx/migrations/sql/20150100000001000000_networks.postgres.up.sql
  ```

  ```
hyperfine --shell=none --warmup=5 "find ./persistence/sql/migrations -name '*.sql' -type f"
Benchmark 1: find ./persistence/sql/migrations -name '*.sql' -type f
  Time (mean ± σ):     206.6 ms ±   4.8 ms    [User: 2.4 ms, System: 5.0 ms]
  Range (min … max):   199.5 ms … 214.4 ms    14 runs
```


Trace calls:

```
pid$target:code.test.before:*NewMigrationBox:entry { self->t = 1}

pid$target:code.test.before:*NewMigrationBox:return { stop() }

pid$target:code.test.before:sort*: /self->t != 0/ {}

```

```
$ sudo dtrace -s ~/scratch/popx_trace_calls.dtrace -c './code.test.before' -F -w

CPU FUNCTION                                 
  7  -> sort.Sort                             
  7    -> sort.pdqsort                        
  7      -> sort.insertionSort                
  7      <- sort.insertionSort                
  7    <- sort.Sort                           
  7    -> sort.Sort                           
  7      -> sort.pdqsort                      
  7        -> sort.insertionSort              
  7        <- sort.insertionSort              
  7      <- sort.Sort                         
  7      -> sort.Sort                         
  7        -> sort.pdqsort                    
  7          -> sort.choosePivot              
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7            -> sort.median                 
  7            <- sort.median                 
  7          <- sort.choosePivot              
  7          -> sort.partialInsertionSort     
  7          <- sort.partialInsertionSort 
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
