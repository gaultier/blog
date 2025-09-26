Title: See all network traffic in a Go program, even encrypted data
Tags: Go, DTrace
---

My most common use of DTrace is to observe I/O data received and sent by various programs. This is so valuable!

However, sometimes this data is encrypted and/or compressed which makes simplistic approaches not viable.

I hit this problem when implementing the [Oauth2](https://en.wikipedia.org/wiki/OAuth) login flow. If you're not familiar, this allows a user with an account on a 'big' website such as Facebook, Google, etc, known as the 'authorization server', to sign-up/login on a third-party website using their existing account, without having to manage additional credentials. In my particular case, this was 'Login with Amazon'. Yes, this exists.

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

`write(2)` is the easiest since it is enough to instrument the entrypoint of the system call. `read(2)` must be done in two steps, at the entrypoint we record what is the pointer to the source data, and in the return probe we know how much data was actually read and we can print it.

What's important is to only trace successful reads/writes (`/ arg0 > 0 /`). Otherwise, we'll try to copy data with a size of `-1` which will be cast as an unsigned number with the maximum value, and result in a `out of scratch space` message from DTrace. Which is DTrace's 'Out Of Memory' case.

Another point: we could be slightly more conservative by also instrumenting the `write(2)` system call in two steps, because technically, the data that is going to be written, has not yet been paged in by the kernel, and thus could result in a page fault when we try to copy it. The solution is to do it in two steps, same as `read(2)`, because at the return point, the data has definitely been paged in. I think this is most likely to happen if you write data from the `.rodata` or `.data` sections of the executable which have not yet been accessed, but I have found that nearly all real-world programs write dynamic data which has already been faulted in. See the [docs](https://illumos.org/books/dtrace/chp-user.html#chp-user) for more details.


### Trace multiple processes

This approach also has the advantage that we can trace multiple processes (by PID or name) in the same script, which is quite nice:

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

## See encrypted data in clear

Most websites use TLS nowadays (`https://`) and will encrypt their data before sending it over the wire. Let's take a simple Go program that makes an HTTP GET request over TLS to demonstrate:

```go
package main

import (
	"crypto/tls"
	"net/http"
)

func main() {
	// Force HTTP v1.
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

Note that some data printed from the `read(2)` and `write(2)` system calls will still inevitably appear gibberish because it corresponds to binary data, for example DNS requests, compressed HTTP bodies, etc. For compressed data, the same trick as for encrypted data can be used.
