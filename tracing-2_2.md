tracing Part II: subsection "eBPF"
================================================================================
## intro

Important files:
* include/uapi/linux/bpf.h
* include/linux/bpf_types.h
* kernel/trace/bpf_trace.c
* include/linux/filter.h
* net/core/filter.c for bpf-networking

`eBPF`: *extended BPF* started as the _classic_ "Berkeley Packet Filter" which attaches to sockets (like tcpdump does, see output of `tcpdump -d`) in ~ 1997. The `old/classic` cBPF started with two 32bit registers, 16 memory slots and limited instruction set. Userspace programs could hand over those instructions/BPF-programs to the kernel where they were verified and executed in kernel space by an intrepreter. Just-in-Time(JIT) for faster execution was added with kernel 3.0 in 2011. `Extended BPF` started with a patchset by Alexei Starovoitov in 2013, began to merge in 2014, like [`the bpf(2)`](#bpf-man) syscall, tracing related additions merged in kernel 4.x.

BPF is often called a [in-kernel virtual machine](#bpfpresentation) meaning the BPF instructions in bytecode are executed by the BPF interpreter or JIT the same way Java bytecode works. Not to be confused with (hardware supported) virtual machine to run multiple guest operating systems.

`eBPF` is now just called `BPF` and is not an acronym for "Berkeley Packet Filter" anymore because it can do more than filtering packets, added more registers (11), 512 byte stack space, "unlimited" map space, bpf helper calls, tracing features (tracepoints, kprobe, uprobes). A `map` is key/value memory area with a defined `BPF_MAP_TYPE_*` and size. Types include HASH, ARRAY, PERF_EVENT, PERCPU_ARRAY, STACK_TRACE, etc. Helper calls are used for map lookups/changes: `bpf_map_{lookup|update|delete}_elem(map,key,value*)`, `bpf_probe_read()`, `bpf_get_current_comm`, etc. 

See [Linux Documentation/networking/filter.txt](#filter.txt) [bpf (2) manual](#bpf-man) or [BPF performance tools book](#bpf-book) for more information. Also the bpf introduction from [cilium](#cilium) might be a good read, especially for the opcodes format. Also two LWN articles: [introduction](#BPFintroduction) and [seccomp](#seccomp) (limiting syscalls with BPF filters).

To use `BPF` you or the tools use the `bpf(2)` syscall and connect programs and maps via file descriptors which when closed normally remove the program/map. E.g.`bpftrace` and `bcc` help in compiling (to BPF-opcodese/instructions) code via LLVM IR, calling the bpf syscall and if the in-kernel verifier accepts it your program with maps will run in kernel-space. See the article "A thorough introduction to eBPF" on LWN or kernel docs' filter.txt on how the [verifier](#lwn) works. 

Also see the presentation from the _inventor_ [Starovoitov 2015](#bpfpresentation).


Example workflow of a tracing BPF program, like the ones invoked via `bpftrace`, using the kernel `libbpf` "bpf loader library" which uses the `bpf(2)` syscall (all caps are enums or preprocessor #defines):

* compile/assemble opcodes or let tools compile a program via LLVM/clang to binary/bytecode
* BPF_MAP_CREATE: create maps as storage area, and get an associated file descriptor
* BPF_PROG_LOAD: load BPF_PROG_TYPE_TRACEPOINT program, write to map via fd 
* kernel verifier does his job 
* BPF program gets attached to tracepoints, kprobes, etc
* e.g. record events via bpf_helper calls, e.g. read from map via fd
* on exit, BPF MAP and PROGRAM gets removed when the associated file descriptors are closed (i.e. the program & map are not "pinned" to stay in memory)

Which BPF_PROG_TYPE_* and BPF_MAP_TYPES_* your kernel-config includes can be seen in [bpf_types.h](#bpf_types.h).

For tracing e.g.: 

```
[...]
#ifdef CONFIG_BPF_EVENTS
BPF_PROG_TYPE(BPF_PROG_TYPE_KPROBE, kprobe,
	      bpf_user_pt_regs_t, struct pt_regs)
BPF_PROG_TYPE(BPF_PROG_TYPE_TRACEPOINT, tracepoint,
	      __u64, u64)`
[...]
BPF_MAP_TYPE(BPF_MAP_TYPE_ARRAY, array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERCPU_ARRAY, percpu_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PROG_ARRAY, prog_array_map_ops)
BPF_MAP_TYPE(BPF_MAP_TYPE_PERF_EVENT_ARRAY, perf_event_array_map_ops)
[...]
BPF_MAP_TYPE(BPF_MAP_TYPE_STACK_TRACE, stack_trace_map_ops)
```

The main header file for  extended bpf is [bpf.h](#bpf.h) in include/uapi/linux/bpf.h together with bpf_common.h. It defines registers, instructions, bpf syscall constants, etc. It also is the source for the [bpf-helpers(7) man page](#bpf-helpers-man).




The eBPF ABI from kernel-doc: [filter.txt](#filter.txt)

Register usage:

```
    * R0	- return value from in-kernel function, and exit value for eBPF program
    * R1 - R5	- arguments from eBPF program to in-kernel function
    * R6 - R9	- callee saved registers that in-kernel function will preserve
    * R10	- read-only frame pointer to access stack
```

Note the calling convention is similar to x86_64 so that a call to internal bpf function can be translated to a normal/hardware `call`.

On x86_64 the register mapping from bpf to hardware:
```
    R0 - rax
    R1 - rdi
    R2 - rsi
    R3 - rdx
    R4 - rcx
    R5 - r8
    R6 - rbx
    R7 - r13
    R8 - r14
    R9 - r15
    R10 - rbp
```

For BPF instructions/opcodes see `filter.txt` and/or `include/linux/filter.h` for macros like the following. Also see the [cilium](#cilium) pages for a good introduction to opcodes and the calling convention to bpf helper functions.

```C
#define BPF_MOV64_REG(DST, SRC)					\
	((struct bpf_insn) {					\
		.code  = BPF_ALU64 | BPF_MOV | BPF_X,		\
		.dst_reg = DST,					\
		.src_reg = SRC,					\
		.off   = 0,					\
		.imm   = 0 })
```

You get the idea how simplistic the ops are constructed: fixed struct filled with hex values.


For a simple program directly using the BPF ops constructing a socket filter `BPF_PROG_TYPE_SOCKET_FILTER` with one `BPF_MAP_TYPE_ARRAY`  see kernel source: `samples/bpf/sock_example.c`.

Main lines are:

create map:
```C
map_fd = bpf_create_map(BPF_MAP_TYPE_ARRAY, sizeof(key), sizeof(value), 256, 0);
```

"Assemble" progam, note how `map_fd` is inserted:
```C

	struct bpf_insn prog[] = {
		BPF_MOV64_REG(BPF_REG_6, BPF_REG_1),
		BPF_LD_ABS(BPF_B, ETH_HLEN + offsetof(struct iphdr, protocol) /* R0 = ip->proto */),
		BPF_STX_MEM(BPF_W, BPF_REG_10, BPF_REG_0, -4), /* *(u32 *)(fp - 4) = r0 */
		BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), /* r2 = fp - 4 */
		BPF_LD_MAP_FD(BPF_REG_1, map_fd),
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
		BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 2),
		BPF_MOV64_IMM(BPF_REG_1, 1), /* r1 = 1 */
		BPF_RAW_INSN(BPF_STX | BPF_XADD | BPF_DW, BPF_REG_0, BPF_REG_1, 0, 0), /* xadd r0 += r1 */
		BPF_MOV64_IMM(BPF_REG_0, 0), /* r0 = 0 */
		BPF_EXIT_INSN(),
	};
```

Load program:                   
```C
	size_t insns_cnt = sizeof(prog) / sizeof(struct bpf_insn);

	prog_fd = bpf_load_program(BPF_PROG_TYPE_SOCKET_FILTER, prog, insns_cnt,
				   "GPL", 0, bpf_log_buf, BPF_LOG_BUF_SIZE);
```

Attach to socket:
```C
	sock = open_raw_sock("lo");
	if (setsockopt(sock, SOL_SOCKET, SO_ATTACH_BPF, &prog_fd,
		       sizeof(prog_fd)) < 0) {
```

Get map elements:
```C
key = IPPROTO_ICMP;
		assert(bpf_map_lookup_elem(map_fd, &key, &icmp_cnt) == 0);

```


`bpftool` is the in kernel-source-tool (in `tools/bpf/bpftool`) to list bpf related programs, maps and more.

From `Documentation/bpf/bpf_devel_QA.rst`:
```
A: The main purpose of bpftool (under tools/bpf/bpftool/) is to provide
a central user space tool for debugging and introspection of BPF programs
and maps that are active in the kernel. If UAPI changes related to BPF
enable for dumping additional information of programs or maps, then
bpftool should be extended as well to support dumping them.
```

Run on one console:
```bash
# strace bpftrace -e 'tracepoint:syscalls:sys_enter_bpf { printf("called %s %d %s\n", comm, pid, probe ); }' |& egrep "perf|bpf"
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
Note: r1 is automatically the bpf context: `ctx`. r10 is stack base pointer.

You can see how both parts, the in-kernel bpf program/map and the userspace tool are connected. From the first output, the bpf syscalls with map entries for every SMT-core and `perf_event_open({type=PERF_TYPE_TRACEPOINT, size=0 /* PERF_ATTR_SIZE_??? */, config=481, ...}, -1, 0, -1, PERF_FLAG_FD_CLOEXEC) = 13`. From the second, the bpf prog dump, the `call bpf_perf_event_output`.



The following is the output of (`biosnoop.py` from bcc/tools) with BTF (BPF type format) information. BCC adds BTF information to the virtual files, bpftrace doesn't. BTF is (approximately) compressed (100:1) dwarf debug information. See (https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html) for a BTF intro.

You need a kernel compiled with `CONFIG_DEBUG_INFO_BTF=y and CONFIG_DEBUG_INFO=y` (check for /sys/kernel/btf/vmlinux), the `pahole` tool to convert dwarf2btf and appropriate libbpf library (from Kernel 5.5+), the tools (bpftool, bpftrace, etc) from distribution packages most likely don't work. Check the file `/sys/kernel/btf/vmlinux`.

Try
`bpftool btf dump file /sys/kernel/btf/vmlinux format c > /tmp/VMlINUX.h`
 to get all structs and pieces of your kernel in the format of a c header file.
From (https://facebookmicrosites.github.io/bpf/blog/2020/02/19/bpf-portability-and-co-re.html#bpf-co-re-user-facing-experience): "This header contains all kernel types: those exposed as part of UAPI, internal types available through kernel-devel, and some more *internal kernel types not available anywhere else*."

BTF is optional but helps understanding/analyzing/debugging bpf programs and for getting the structs the running kernel is using.

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



There are kernel internal bpf helper functions called [bpf-helpers](#bpf-helpers-man) like `bpf_map_lookup_elem` or `bpf_map_update_elem` to read/change map values, to get the time: `bpf_ktime_get_ns`, `bpf_trace_printk` to print to the debugging trace-ring-buffer which can be used in bpf programs.

From [man-page](#bpf-helpers-man): 
```
...
    Since there are several eBPF program types, and that they do
    not run in the same context, each program type can only call a subset
    of those helpers.
...
```

And there is the libbpf library from kernel source in `tools/lib/bpf` which is used by tools like `bpftrace` and `BCC` to load the tracing eBPF programs. See `tools/lib/bpf` and (https://github.com/libbpf/libbpf) especially for `BPF CO-RE: Compile Once – Run Everywhere` which uses BTF information and does not need the LLVM/Clang installed e.g. on embedded systems or for performance reasons.



Good overview which bpf features were added when: (https://github.com/iovisor/bcc/blob/master/docs/kernel-versions.md). And also which bpf program type can acces which bpf helper functions.


NOTE: you can read some kernel memory, like struct fields, filename char * provided by tracepoints and the like. But you can't read arbitrary kernel memory if it would lead to a page fault. E.g. reading the filenames from the open-fd-table is currentyl not possible so is accessing filenames from struct file*. Two bpf helpers will be added _soon_ (as of july 2020, check kernel mail net/bpf mailing list).


===
## Links:

1. <a name="bpf.h"></a> most important: bpf header file (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/uapi/linux/bpf.h)
1. <a name="filter.txt"></a> kernel: Documentation/networking/filter.txt (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/networking/filter.txt)
1. <a name="bpf-man"></a> bpf(2) man page   (http://www.man7.org/linux/man-pages/man2/bpf.2.html)   
1. <a name="bpf-helpers-man"></a> bpf-helpers man page (https://www.man7.org/linux/man-pages/man7/bpf-helpers.7.html) 
1. <a name="bpf-book"></a> "BPF Performance Tools", Brendan Gregg, 2019 (ISBN-10: 0-13-655482-2)     
1. <a name="btf-intro"></a> BTF introduction      (https://facebookmicrosites.github.io/bpf/blog/2018/11/14/btf-enhancement.html)
1. <a name="bpf_types.h"></a>"bpf_types.h"     (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/linux/bpf_types.h)
1. <a name="lwn"></a> A thorough introduction to eBPF      (https://lwn.net/Articles/740157/)
1. <a name="cilium"></a> bpf introduction from cilium      (https://docs.cilium.io/en/latest/bpf/)
1. <a name="bpfpresentation"></a> presentation by Starovoitov: "BPF -- in-kernel virtual machine (http://vger.kernel.org/netconf2015Starovoitov-bpf_collabsummit_2015feb20.pdf)



1. <a name ="BPFintroduction"></a> LWN: "A thorough introduction to eBPF" (https://lwn.net/Articles/740157/)

1. <a name="seccomp"></a> LWN: "A seccomp overview" (https://lwn.net/Articles/656307/)

1. <a name="xdpetc"></a> cilium: native&offloading XDP to NICs (https://docs.cilium.io/en/latest/bpf/#xdp)

1. <a name="readinglist"></a> bpf reading list (https://qmonnet.github.io/whirl-offload/2016/09/01/dive-into-bpf/)