Title: See all network traffic in a Go program, even encrypted data
Tags: Go, DTrace
---

*For a gentle introduction to DTrace especially in conjunction with Go, see my past article: [An optimization and debugging story with Go and DTrace](/blog/an_optimization_and_debugging_story_go_dtrace.html).*

My most common use of DTrace is to observe I/O data received and sent by various programs. This is so valuable and that's why I started using DTrace in the first place!

However, sometimes this data is encrypted and/or compressed which makes simplistic approaches not viable.

I hit this problem when implementing the [OAuth2](https://en.wikipedia.org/wiki/OAuth) login flow. If you're not familiar, this allows a user with an account on a 'big' website such as Facebook, Google, etc, known as the 'authorization server', to sign-up/login on a third-party website using their existing account, without having to manage additional credentials. In my particular case, this was 'Login with Amazon' (yes, this exists).

Since there are 3 actors exchanging data back and forth, this is paramount to observe all the data on the wire to understand what's going on (and what's going wrong). The catch is, for security, most of this data is sent over TLS (HTTPS), meaning, encrypted. Thankfully we can use DTrace to still see the data in clear.

Let's see how, one step at a time.

## Level 1: Observe all write/read system calls

This is the easiest and also what `iosnoop` or `dtruss` do.


The simplest form is this:

```dtrace
#pragma D option strsize=16K

syscall::write:entry
/ pid == $target && arg0 > 0/
{
   printf("fd=%d len=%d data=%s\n", arg0, arg2, stringof(copyin(arg1, arg2)));
}


syscall::read:entry
/ pid == $target /
{
  self->read_ptr = arg1;
  self->read_fd = arg0;
}

syscall::read:return
/ pid == $target && self->read_ptr!=0 && arg0 > 0 /
{
  printf("fd=%d len=%d data=%s\n", self->read_fd, arg0, stringof(copyin(self->read_ptr, arg0)));

  self->read_ptr = 0;
  self->read_fd = 0;
}
```

We run it like this: `sudo dtrace -s myscript.d -p <pid>` or `sudo dtrace -s myscript.d -c ./myprogram`.

`write(2)` is the easiest since it is enough to instrument the entry point of the system call. `read(2)` must be done in two steps, at the entry point we record what is the pointer to the source data, and in the return probe we know how much data was actually read and we can print it.

What's important is to only trace successful reads/writes (`/ arg0 > 0 /`). Otherwise, we'll try to copy data with a size of `-1` which will be cast to an unsigned number with the maximum value, and result in a `out of scratch space` message from DTrace. Which is DTrace's own 'Out Of Memory' case. Fortunately, DTrace has been designed up-front to handle misbehaving D scripts so that they do not impact the stability of the system, so there are not other consequences than our action failing.

Another point: we could be slightly more conservative by also instrumenting the `write(2)` system call in two steps, because technically, the data that is going to be written, has not yet been paged in by the kernel, and thus could result in a page fault when we try to copy it. The solution is to do it in two steps, same as `read(2)`, because at the return point, the data has definitely been paged in. I think this is most likely to happen if you write data from the `.rodata` or `.data` sections of the executable which have not yet been accessed, but I have found that nearly all real-world programs write dynamic data which has already been faulted in. The [docs](https://illumos.org/books/dtrace/chp-user.html#chp-user) mention this fact.


### Trace multiple processes

Observing system calls system-wide also has the advantage that we can trace multiple processes (by PID or name) in the same script, which is quite nice:

```dtrace

syscall::write:entry
/ pid == 123 || pid == 456 || execname == "curl" /
{
   printf("fd=%d len=%d data=%s\n", arg0, arg2, stringof(copyin(arg1, arg2)));
}
```

### Trace all system calls for networking

Multiple probes can be grouped with commas when they share the same action, so we can instrument *all* system calls that do networking in a compact manner. Fortunately, they share the same first few arguments in the same order. I did not list every single one here, this just for illustrative purposes:

```dtrace
syscall::write:entry, syscall::sendto_nocancel:entry, syscall::sendto:entry 
/ pid == $target && arg0>2 && arg0 > 0/
{
   printf("fd=%d len=%d data=%s\n", arg0, arg2, stringof(copyin(arg1, arg2)));
}


syscall::read:entry, syscall::recvfrom_nocancel:entry, syscall::recvfrom:entry 
/ pid == $target /
{
  self->read_ptr = arg1;
  self->read_fd = arg0;
}

syscall::read:return, syscall::recvfrom_nocancel:return, syscall::recvfrom:return 
/ pid == $target && arg0 > 0 && self->read_ptr!=0 /
{
  printf("fd=%d len=%d data=%s\n", self->read_fd, arg0, stringof(copyin(self->read_ptr, arg0)));

  self->read_ptr = 0;
  self->read_fd = 0;
}
```

## Level 2: Only observe network data

A typical program will print things on stderr and stdout, load files from disk, and generally do I/O that's not related to networking, and this will show up in our script.

The approach I have used is to record which file descriptors are real sockets. We can do this by  tracing the `socket(2)` or `listen(2)` system calls and recording the file descriptor in a global map. Then, we only trace I/O system calls that operate on these file descriptors:

```diff
+ syscall::socket:return, syscall::listen:return
+ / pid == $target && arg0 != -1 /
+ {
+   socket_fds[arg0] = 1;
+ }
+ 
+ syscall::close:entry
+ / pid == $target && socket_fds[arg0] != 0 /
+ {
+   socket_fds[arg0] = 0;
+ }

syscall::write:entry, syscall::sendto_nocancel:entry, syscall::sendto:entry 
- / pid == $target && arg1 != 0 /
+ / pid == $target && arg1 != 0 && socket_fds[arg0] != 0 /
{ 
  // [...]
}

syscall::read:entry, syscall::recvfrom_nocancel:entry, syscall::recvfrom:entry 
- / pid == $target /
+ / pid == $target && socket_fds[arg0] != 0 /
{ 
  // [...]
}
```

When a socket is closed, we need to remove it from our set, since this file descriptor could be used later for a file, etc.

This trick is occasionally useful, but this script has one big requirement: it must start before a socket of interest is opened by the program, otherwise this socket will not be traced. For a web server, if we intend to only trace new connections, that's completely fine, but for a program with long-lived connections that we cannot restart, that approach will not work as is.

In this case, we could first run `lsof -i TCP -p <pid` to see the file descriptor for these connections and then in the `BEGIN {}` clause of our D script, add these file descriptors to the `socket_fds` set manually.

## Level 3: See encrypted data in clear

Most websites use TLS nowadays (`https://`) and will encrypt their data before sending it over the wire. Let's take a simple Go program that makes an HTTP GET request over TLS to demonstrate:

```go
package main

import (
	"crypto/tls"
	"net/http"
)

func main() {
	// Force HTTP 1.
	http.DefaultClient.Transport = &http.Transport{TLSNextProto: make(map[string]func(authority string, c *tls.Conn) http.RoundTripper)}

	http.Get("https://google.com")
}
```

*I force HTTP 1 because most people are familiar with it, and the data is text, contrary to HTTP 2 or 3 which use binary. But this is just for readability, the DTrace script works either way. For binary data, `tracemem` should be used to print a hexdump.*

When using our previous approach, we only see gibberish, as expected:

```text
9    176                      write:entry size=1502 fd=8: �
10    175                      read:return size=1216 fd=8: ��O��b�%�e�ڡXw2d�q�$���NX�d�P�|�`HY-To�dpK	�x�8to�DO���_ibc��y��]�ވe��
                                                 �c��ҿ��^�L#��Ue	
```

How can we see the data in clear? Well, we need to find where the data gets encrypted/decrypted in our program and observe the input/output, respectively.

In a typical Go program, the functions of interest are `crypto/tls.(*halfConn).decrypt` and `crypto/tls.(*halfConn).encrypt`. Since they are private functions, there is a risk that the Go compiler would inline them which would make them invisible to DTrace. But since they have relatively long and complex bodies, this is unlikely.

Their signatures are: 

- `func (hc *halfConn) decrypt(record []byte) ([]byte, recordType, error)`
- `func (hc *halfConn) encrypt(record, payload []byte, rand io.Reader) ([]byte, error)`

By trial and error, we instrument these functions so:

```dtrace
pid$target::crypto?tls.(?halfConn).decrypt:return
{
  printf("%s\n", stringof(copyin(arg1, arg0)));
}

pid$target::crypto?tls.(?halfConn).encrypt:entry
{
  printf("%s\n", stringof(copyin(arg4, arg3)));
}
```

We need to replace the `*` character by `?` since `*` is interpreted by DTrace as a wildcard (any character, any number of times) in the probe name. `?` is interpreted as: any character, once.

And boom, we can now see the data in clear:

```text
 12 273146 crypto/tls.(*halfConn).encrypt:entry GET / HTTP/1.1
Host: google.com
User-Agent: Go-http-client/1.1
Accept-Encoding: gzip


  1 273145 crypto/tls.(*halfConn).decrypt:return HTTP/1.1 200 OK
Date: Thu, 25 Sep 2025 15:39:00 GMT
Expires: -1
Cache-Control: private, max-age=0
Content-Type: text/html; charset=ISO-8859-1
Content-Security-Policy-Report-Only: object-src 'none';base-uri 'self';script-src 'nonce-NNo3yQEGH1Cydlkm4FuhSg' 'strict-dynamic' 'report-sample' 'unsafe-eval' 'unsafe-inline' https: http:;report-uri https://csp.withgoogle.com/csp/gws/other-hp
Accept-CH: Sec-CH-Prefers-Color-Scheme
P3P: CP="This is not a P3P policy! See g.co/p3phelp for more info."
Content-Encoding: gzip
Server: gws
X-XSS-Protection: 0
X-Frame-Options: SAMEORIGIN
Set-Cookie: AEC=AaJma5t2IauygzCrcZIEVudn3SEoGHoVuevRl4vUfxpCR5b6Hnusm3RgLIU; expires=Tue, 24-Mar-2026 15:39:00 GMT; path=/; domain=.google.com; Secure; HttpOnly; SameSite=lax
Set-Cookie: __Se
```

## Level 4: See compressed data in clear

For compressed data, the same trick as for encrypted data can be used: find the encode/decode functions and print the input/output, respectively.

Let's take a contrived example where our program sends a random, gzipped string over the network:

```go
package main

import (
	"compress/gzip"
	"crypto/rand"
	"crypto/tls"
	"net/http"
	"strings"
)

func main() {
	// Force HTTP 1.
	http.DefaultClient.Transport = &http.Transport{TLSNextProto: make(map[string]func(authority string, c *tls.Conn) http.RoundTripper)}

	w := strings.Builder{}
	gzipWriter := gzip.NewWriter(&w)
	msg := "hello " + rand.Text()
	gzipWriter.Write([]byte(msg))
	gzipWriter.Close()

	http.Post("http://google.com", "application/octet-stream", strings.NewReader(w.String()))
}
```

If we run our previous D script, we can see the HTTP headers just fine, but the body is gibberish (as expected):

```text
  9    176                      write:entry action=write pid=46367 execname=go-get size=208 fd=6: POST / HTTP/1.1
Host: google.com
User-Agent: Go-http-client/1.1
Content-Length: 56
Content-Type: application/octet-stream
Accept-Encoding: gzip

�
```

We can trace the method `compress/gzip.(*Writer).Write` to print its input:

```dtrace
pid$target::compress?gzip.(?Writer).Write:entry 
{
  printf("%s\n", stringof(copyin(arg1, arg2)));
}
```

And we see:

```text
 10 530790 compress/gzip.(*Writer).Write:entry hello ZYDEDSRZBY7D65DQHWWVSUOVJ5
```

## Conclusion

All of these approaches can be used in conjunction to see data that has been first compressed, perhaps hashed, then encrypted, etc.

Once again, DTrace shines by its versatility compared to other tools such as Wireshark (can only observe data on the wire, if it's encrypted then tough luck) or `strace` (can only see system calls). It can programmatically inspect user-space memory, kernel memory, get the stack trace, etc.

Note that some data printed from the `read(2)` and `write(2)` system calls will still inevitably appear gibberish because it corresponds to a binary protocol, for example DNS requests. 


Finally, let's all note that none of this intricate dance would be necessary, if there were some DTrace static probes judiciously placed in the Go standard library or runtime (wink wink to the Go team).
