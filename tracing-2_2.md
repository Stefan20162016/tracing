tracing Part II: subsection "eBPF"
================================================================================
## intro

`eBPF`: *extended BPF* started as the "Berkeley Packet Filter" which attaches to sockets (like tcpdump does, see output of `tcpdump -d`) in ~ 1997. The `old/classic` BPF started with two 32bit registers, 16 memory slots and limited instruction set. Userspace programs could hand over those instructions/BPF-programs to the kernel where they were verified and executed in kernel space by an intrepreter. Just-in-Time(JIT) for faster execution was added with kernel 3.0 in 2011. `Extended BPF` started with a patchset by Alexei Starovoitov in 2013, began to merge in 2014, like [`the bpf(2)`](#bpf-man) syscall, tracing related additions merged in kernel 4.x.

`eBPF` is now just called `BPF` and is not an acronym for "Berkeley Packet Filter" anymore because it can do more than filtering packets, added more registers (10), 512 byte stack space, "unlimited" map space, bpf helper calls, the known tracing features (tracepoints, kprobe, uprobes). A `map` is key/value memory area with a defined `BPF_MAP_TYPE_*` and size. Types include HASH, ARRAY, PERF_EVENT, PERCPU_ARRAY, STACK_TRACE, etc. Helper calls are used for map lookups/changes: `bpf_map_{lookup|update|delete}_elem(map,key,value*)`, `bpf_probe_read()`, `bpf_get_current_comm`, etc. 

See [Linux Documentation/networking/filter.txt](#filter.txt) [bpf (2) manual](#bpf-man) or [BPF performance tools book](#bpf-book) for more information.

To use `BPF` you or the tools use the `bpf(2)` syscall. E.g.`bpftrace` and `bcc` help in compiling code via LLVM IR, calling the bpf syscall and if the in-kernel verifier accepts it your program with maps runs in kernel-space.


Example workflow of a tracing BPF program, like the ones invoked via `bpftrace`, using the kernel `libbpf` "bpf loader library" which uses the `bpf(2)` syscall :

* BPF_MAP_CREATE: create maps as storage area
* BPF_PROG_LOAD: load BPF_PROG_TYPE_TRACEPOINT program
* kernel verifier does his job 
* BPF program gets attached to tracepoints, kprobes, etc
* e.g. record events via bpf_helper calls 
* on exit, BPF MAP and PROGRAM gets removed when the associated file descriptors are closed (i.e. the program&map is not "pinned" to stay in memory)

For a simple program doing these steps and using raw bpf instructions see kernel source: `samples/bpf/sock_example.c`.

`bpftool` is the in kernel-source-tool (in `tools/bpf/bpftool`) to list bpf related programs, maps and more.

From `Documentation/bpf/bpf_devel_QA.rst`:
```
Q: When should I add code to the bpftool?
-----------------------------------------
A: The main purpose of bpftool (under tools/bpf/bpftool/) is to provide
a central user space tool for debugging and introspection of BPF programs
and maps that are active in the kernel. If UAPI changes related to BPF
enable for dumping additional information of programs or maps, then
bpftool should be extended as well to support dumping them.
```

Run on one console:
```bash
# bpftrace -e 'tracepoint:syscalls:sys_enter_bpf {printf("called %s %d %s\n", comm, pid, probe );}'
```
On another:
```bash
# ./bpftool prog show
[...]
16: tracepoint  name sys_enter_bpf  tag a486b336ea4cc79b  gpl
	loaded_at 2020-05-15T17:56:08+0200  uid 0
	xlated 248B  jited 155B  memlock 4096B  map_ids 15
# ./bpftool prog dump xlated id 16
   0: (bf) r6 = r1
   1: (b7) r7 = 0
   2: (7b) *(u64 *)(r10 -56) = r7
   3: (7b) *(u64 *)(r10 -48) = r7
   4: (7b) *(u64 *)(r10 -40) = r7
   5: (7b) *(u64 *)(r10 -16) = r7
   6: (7b) *(u64 *)(r10 -8) = r7
   7: (bf) r1 = r10
   8: (07) r1 += -16
   9: (b7) r2 = 16
  10: (85) call bpf_get_current_comm#107264
  11: (79) r1 = *(u64 *)(r10 -16)
  12: (7b) *(u64 *)(r10 -48) = r1
  13: (79) r1 = *(u64 *)(r10 -8)
  14: (7b) *(u64 *)(r10 -40) = r1
  15: (85) call bpf_get_current_pid_tgid#107088
  16: (77) r0 >>= 32
  17: (7b) *(u64 *)(r10 -32) = r0
  18: (7b) *(u64 *)(r10 -24) = r7
  19: (18) r7 = map[id:17]
  21: (85) call bpf_get_smp_processor_id#106832
  22: (bf) r4 = r10
  23: (07) r4 += -56
  24: (bf) r1 = r6
  25: (bf) r2 = r7
  26: (bf) r3 = r0
  27: (b7) r5 = 40
  28: (85) call bpf_perf_event_output_tp#-56800
  29: (b7) r0 = 0
  30: (95) exit


# echo for program flow visualization:
# ./bpftool prog dump xlated id 18  visual > out.dot && dot -Tpng out.dot > out.png

```

A different program (`biosnoop.py` from bcc/tools) with BTF information. BTF is approx. compressed (100:1) dwarf debug information. See (https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html) for a BTF intro.
You need to a kernel compiled with `CONFIG_DEBUG_INFO_BTF=y and CONFIG_DEBUG_INFO=y` (check for /sys/kernel/btf/vmlinux) and appropriate libbpf library  (from Kernel 5.5+) .
Also try `bpftool btf dump file /sys/kernel/btf/vmlinux format c > /tmp/VMlINUX.h` to get all structs and pieces of your kernel.
From (https://facebookmicrosites.github.io/bpf/blog/2020/02/19/bpf-portability-and-co-re.html#bpf-co-re-user-facing-experience): "This header contains all kernel types: those exposed as part of UAPI, internal types available through kernel-devel, and some more *internal kernel types not available anywhere else*."

```bash
# bpftool prog dump xlated id 391 linum

[...]
; bpf_map_delete_elem((void *)bpf_pseudo_fd(1, -1), &req); [file:/virtual/main.c line_num:56 line_col:33]
 107: (18) r1 = map[id:166]
 109: (bf) r2 = r10
 110: (07) r2 += -8
; bpf_map_delete_elem((void *)bpf_pseudo_fd(1, -1), &req); [file:/virtual/main.c line_num:56 line_col:5]
 111: (85) call htab_map_delete_elem#117440
; } [file:/virtual/main.c line_num:58 line_col:1]
 112: (b7) r0 = 0
 113: (95) exit
```


```bash
# bpftool prog dump jited id 391 linum

[...]
; bpf_map_delete_elem((void *)bpf_pseudo_fd(1, -1), &req); [file:/virtual/main.c line_num:56 line_col:33]
 1f9:	movabs $0xffff9f840f792c00,%rdi
 203:	mov    %rbp,%rsi
 206:	add    $0xfffffffffffffff8,%rsi
; bpf_map_delete_elem((void *)bpf_pseudo_fd(1, -1), &req); [file:/virtual/main.c line_num:56 line_col:5]
 20a:	callq  0xffffffffd2088074
; } [file:/virtual/main.c line_num:58 line_col:1]
 20f:	xor    %eax,%eax
 211:	pop    %rbx
 212:	pop    %r15
 214:	pop    %r14
 216:	pop    %r13
 218:	pop    %rbx
 219:	leaveq 
 21a:	retq   
```



===
## Links:

1. <a name="filter.txt"></a> kernel: Documentation/networking/filter.txt (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/networking/filter.txt)
1. <a name="bpf-man"></a> bpf(2) man page   (http://www.man7.org/linux/man-pages/man2/bpf.2.html)    
1. <a name="bpf-book"></a> "BPF Performance Tools", Brendan Gregg, 2019 (ISBN-10: 0-13-655482-2)     
1. <a name="btf-intro"></a> "BTF introduction"      (https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html)
1. <a name=""></a> description      ()
1. <a name=""></a> description      ()
1. <a name=""></a> description      ()
1. <a name=""></a> description      ()




