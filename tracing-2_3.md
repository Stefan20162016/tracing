tracing Part II: subsection "bpftrace"
================================================================================
## bpftrace intro

Main repositort for bpftrace (and also BCC: BPF compiler collection) is https://github.com/iovisor/bpftrace ( resp https://github.com/iovisor/bcc)

See (https://github.com/iovisor/bpftrace/) how to install bpftrace.

bpftrace has short awk-like syntax, executes until you press CTRL+c, use "timeout -s SIGINT 10 bpftrace prog.bt"
so you dont have to cleanup the BPF programm afterwards. 

It uses libbpf (part of kernel) and libbcc (from bcc iovisor project). Install both before if you are compiling from scratch (using llvm 9 worked on ubuntu 1910 eon).

Features include:

* Builtin-variables: pid(kernel tgid), tid(kernel pid), uid, username, nsecs(timestamp), elapsed, cpu, comm(string),
kstack, ustack, arg0...argN, args, retval(kprobes,uprobes), func, probe, curtask, cgroup, $1...$N.
See (https://github.com/iovisor/bpftrace/blob/master/docs/reference_guide.md#1-builtins).

* @maps are hash-maps which are automatically per-cpu and reduced/combined together to print the output.
* count()
* histograms
* kernel symbol/address functions: ksym, kaddr
* kernel & user stacks
* arguments to function arg0...argN or args-> (depending on type)

It can probe e.g.: kprobes, tracepoints, usdt's, soft- & hardware(PMCs) "events/instrumentation/traceable points". see: (https://github.com/iovisor/bpftrace/blob/master/docs/reference_guide.md#probes)or `man bpftrace`:

![probes](https://raw.githubusercontent.com/iovisor/bpftrace/master/images/bpftrace_probes_2018.png)

```
# bpftrace -l|grep  kprobe:|wc -l
50682

# bpftrace -l|grep tracepoint:|wc -l
1704

# bpftrace -l|grep software|wc -l
11

# bpftrace -l|grep hardware|wc -l
10
```

file structure:

```
BEGIN {}                        // optional

<one-of-the-traceable-events>   // mandatory
/<filter>/                      // optional
{
    printf("output\n")          // mandatory
}
END {}                          // optional
```

e.g.:
```
tracepoint:syscalls:sys_enter_connect
{
    printf("%s fired %s\n", comm, probe)
}
```
```
Output:
Attaching 1 probe...
nc fired tracepoint:syscalls:sys_enter_connect
```


or with histograms:
```
#!/usr/local/bin/bpftrace

BEGIN{
     printf("once at start\n"); 
}

tracepoint:raw_syscalls:sys_enter                       // event/tracepoint/kprobe/usdt...
/args->id >= 42 && args->id < 52/                       // filter to reduce events

{
//printf("%s %s %d %d\n", probe, comm, pid, args->id ); // print built-in variables and argument to
                                                        // sys_enter tracepoint

@[comm] = lhist(args->id,42,52,1);                      // linear histogram of args->id's from 42 to 52 step 1

}
END{ 
    printf("once at the end\n"); 
}

```

```
@[Chrome_ChildIOT]: 
[42, 43)               2 |                                                    |
[43, 44)               0 |                                                    |
[44, 45)              74 |@@@                                                 |
[45, 46)               6 |                                                    |
[46, 47)              10 |                                                    |
[47, 48)            1145 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
[48, 49)               0 |                                                    |
[49, 50)               0 |                                                    |
[50, 51)               0 |                                                    |
[51, 52)               1 |                                                    |

@[SGI_video_sync]: 
[47, 48)            1602 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@[Xorg]: 
[47, 48)            4383 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@[code]: 
[44, 45)               6 |                                                    |
[45, 46)               0 |                                                    |
[46, 47)               0 |                                                    |
[47, 48)            7607 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|

@[gnome-shell]: 
[47, 48)           16949 |@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@|
```

You can `interval`s e.g.:
```
tracepoint:raw_syscalls:sys_enter                     // event/tracepoint/kprobe/usdt...
/args->id >= 42 && args->id < 52/                     // filter to reduce events
{
@[comm] = lhist(args->id,42,52,1); 
}
interval:s:1{
    print(@)
}
```

Change `42` to the syscall number you want the name of.
```
# bpftrace -e 'BEGIN{ printf("%s\n", ksym( *(kaddr("sys_call_table") + 42 * 8)))  ; }'
stdin:1:55-56: WARNING: arithmetic on integers of different signs: 'unsigned int64' and 'int64' can lead to undefined behavior
BEGIN{ printf("%s\n", ksym( *(kaddr("sys_call_table") + 42 * 8)))  ; }
                                                      ~
Attaching 1 probe...
sys_connect
^C

```

Or use the built-in functions ksym/kaddr directly (histograms only accept integers):


```
bpftrace -e 'tracepoint:raw_syscalls:sys_enter { @[comm, pid, ksym( *(kaddr("sys_call_table") + args->id * 8))] = count(); }'
stdin:1:71-72: WARNING: arithmetic on integers of different signs: 'unsigned int64' and 'int64' can lead to undefined behavior
tracepoint:raw_syscalls:sys_enter { @[ksym( *(kaddr("sys_call_table") + args->id * 8))] = count(); }

Attaching 1 probe...
^C
...
@[Xorg, 2925, sys_recvmsg]: 968
@[gnome-shell, 3631, sys_poll]: 1006
@[EMT-7, 12809, sys_ioctl]: 1935
@[gnome-shell, 3631, sys_getpid]: 1943
@[gnome-shell, 3631, sys_recvmsg]: 3600
@[gnome-shell, 3631, sys_sched_yield]: 24547
```


For more examples see the bpftrace repository: https://github.com/iovisor/bpftrace/tools/ and the corresponding *_example.txt files.



bpftrace -d LLVMIR -e 'tracepoint:syscalls:sys_exit_setpgid {printf("%s %d %d %d\n", comm, pid, args->__syscall_nr,args->ret);  }'




## links

* (https://github.com/iovisor/bpftrace)
* Dtrace (http://dtrace.org/guide/chp-intro.html#chp-intro)
