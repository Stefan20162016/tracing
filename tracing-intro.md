tracing Part 1: Introduction
================================================================================


bcc & bpftrace party tricks
--------------------------------------------------------------------------------

To get you interested a few short examples (some snippets require newer kernels):
```
on one console:
./inject.py kmalloc '__x64_sys_mount()' 

and on another, try mounting a filesystem:

# mount /dev/sdb1 /mnt
mount: /mnt: mount(2) system call failed: Cannot allocate memory.
```

inject.py is a tool from the BCC toolkit which is a python wrapper for bpf libraries (libbcc, libbpf). The next examples use bpftrace which is a seperate tool with awk-like syntax. More of a simple scripting tool with way easier access to bpf functionality. Both use the same libraries and have almost the same capabilities.



```
bpftrace -e 'k:__x64_sys_getuid /comm == "id"/ { override(1337); }' --unsafe
and on another console:
# id
uid=1337 gid=0(root) euid=0(root) groups=0(root)

or use the -c switch:

bpftrace -v -e 'k:__x64_sys_getuid /comm == "id"/ { override(1337); }' --unsafe -c "/usr/bin/id"

```
You guessed correctly. The two snippets override the return value using bpf_override_return() which completely overwrites the syscall and just returns the int value.
(Press CTRL+C to cancel and note that override only works for functions marked ALLOW_ERROR_INJECTION, mostly syscalls and just a few others and it can only return an unsigned long.)

To use bpftrace scripts for a duration of a few seconds use "timeout --signal=SIGINT bpftrace ..." and don't forget the signal or else the bpf program will stay in kernel space and will still be active.

You would like to see what someone else is doing on your system? Just use the following script.
Save and change "pts42" to the pts# you would like to monitor.
```
#!/usr/bin/env bpftrace
#include <linux/tty.h>
kprobe:pty_write{
$ttystruct = (struct tty_struct *)arg0;

if($ttystruct->name == "pts42"){
	printf("test: %s %s\n", $ttystruct->name, str(arg1));
}
}
```


Quick way to get the size of kernel structs:
```
bpftrace -e '#include <linux/sched.h>  \ 
BEGIN { printf("%d\n", sizeof(struct task_struct)); }  '
Attaching 1 probe...
9152
^C

or use --include:
bpftrace --include 'linux/sched.h' -e 'BEGIN { printf("%d\n", sizeof(struct task_cputime)); }  '
Attaching 1 probe...
24
^C

```



What's the kernel doing exactly? GDB only gets you so far, maybe till the init(1) process because the setup code is linear and not concurrent. But to get a feel for the up to millions of function calls per second you need better tools: BCC and/or bpftrace. Two newer tools popularized by Brendan Gregg (*www.brendangregg.com), explained in lenghty detail in his book: "BPF Performance Tools", 2019 (ISBN-10: 0-13-655482-2) and on lots of blog posts. Of the two, bpftrace is my weapon of choice because the syntax is easy and sufficient for a lot of stuff.

![bpftrace-overview](https://raw.githubusercontent.com/iovisor/bpftrace/master/images/bpftrace_internals_2018.png)


As you can see from the diagram, bpftrace is quite involved. There are a few paths from the left userspace tool "bpftrace" till you get to the tracing infrastructure on the right in kernel space. To get ahold of all the bits and pieces let's start with what kernel offers and how the tools access it. And at the end how (complicated) BPF tools like bpftrace and BCC make tracing more efficient.


Next subchapter covers [tracepoints](tracing-1_1.md).

## Links:
1. <a name="bpftrace"></a> bpftrace (https://github.com/iovisor/bpftrace)
1. <a name="BCC"></a> BCC (https://github.com/iovisor/bcc)
1. <a name="bgregg"></a> Brendan Gregg Homepage/Blog (www.brendangregg.com)



