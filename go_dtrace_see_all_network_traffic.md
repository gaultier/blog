Title: See all network traffic in a Go program, even encrypted data
Tags: Go, DTrace
---

## Level 1: Observe all write/read system calls

This is the easiest and also what `iosnoop` does. This is in fact how I came to DTrace: I wanted to see the I/O data my program was sending to and receiving from the world.


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

Another point: we could be slightly more conservative by also instrumenting the `write(2)` system call in two steps, because technically, the data that is going to be written, has not yet been paged in by the kernel, and thus could result in a page fault when we try to copy it. The solution is to do it in two steps, same as `read(2)`, because at the return point, the data has definitely been paged in. I think this is most likely to happen if you write data from the `.rodata` or `.data` sections of the executable which have not yet been accessed, but in most situations, real-world programs write dynamic data which has already been faulted in. See the [docs](https://illumos.org/books/dtrace/chp-user.html#chp-user) for more details.


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

Multiple probes can be grouped with commas when they share the same action, so we can instrument *all* system calls that do networking:

```dtrace
syscall::write:entry, syscall::sendto_nocancel:entry, syscall::sendto:entry 
/ pid == $target && arg0>2 && arg1 != 0 && arg2 >16 /
{
   printf("action=write pid=%d execname=%s size=%d fd=%d: %s\n", pid, execname, arg2, arg0, stringof(copyin(arg1, arg2)));
}


syscall::read:entry, syscall::recvfrom_nocancel:entry, syscall::recvfrom:entry 
/ pid == $target && arg0>2 /
{
  self->read_ptr = arg1;
  self->read_fd = arg0;
}

syscall::read:return, syscall::recvfrom_nocancel:return, syscall::recvfrom:return 
/ pid == $target && arg0>2 && self->read_ptr!=0 /
{
  printf("action=read pid=%d execname=%s size=%d fd=%d: %s\n", pid, execname, arg0, self->read_fd, stringof(copyin(self->read_ptr, arg0)));
  self->read_ptr = 0;
  self->read_fd = 0;
}


