tracing Part I subsection 2:  Tracepoints
================================================================================

Tracepoints: kernel 2.6.28 or 2.6.32 ~2009  started as *kernel markers* ?

From the Documentation: https://github.com/torvalds/linux/blob/v5.6/Documentation/trace/tracepoints.rst we learn that tracepoints are statically defined points in the kernel you can trace:

```
Purpose of tracepoints
----------------------
A tracepoint placed in code provides a hook to call a function (probe)
that you can provide at runtime. A tracepoint can be "on" (a probe is
connected to it) or "off" (no probe is attached). When a tracepoint is
"off" it has no effect, except for adding a tiny time penalty
(checking a condition for a branch) and space penalty (adding a few
bytes for the function call at the end of the instrumented function
and adds a data structure in a separate section).  When a tracepoint
is "on", the function you provide is called each time the tracepoint
is executed, in the execution context of the caller. When the function
provided ends its execution, it returns to the caller (continuing from
the tracepoint site).

You can put tracepoints at important locations in the code. They are
lightweight hooks that can pass an arbitrary number of parameters,
which prototypes are described in a tracepoint declaration placed in a
header file.

They can be used for tracing and performance accounting.
```

"A function (probe) that you can provide at runtime" might be misleading and probably means that you can do that in a kernel module, major subsystem tracepoints like in `sched` are pretty hardcoded. You can turn them on/off at runtime.

Tracepoints can be used in the "ftrace" directory /sys/kernel/debug/tracing, perf and via BPF tools.

Tracepoints are sorted by subsytem:eventname, following the example in the comments in [include/linux/tracepoint.h](#tracepoint.h) we'll look at `tracepoint:sched:sched_switch`. It's using the  TRACE_EVENT(...) macro mentioned at the end of https://github.com/torvalds/linux/blob/v5.6/Documentation/trace/tracepoints.rst and which is also explained in an [LWN Article series part1-3](#LWN_trace_event). See especially part3 for a silly-demo-module which adds a silly-tracepoint.
(note had to change one line in the silly Makefile to compile without error (kernel 5.4.34, ubuntu 20.04) : `kernel_modules: \
	$(MAKE) -C $(KDIR) M=${PWD} EXTRA_CFLAGS='${EXTRA_CFLAGS}' modules`).

See https://github.com/torvalds/linux/tree/master/include/trace/events where all the tracepoints are defined in the *.h files.

Tracepoints offers a fixed set of arguments which you can look up in the corresponding `format` files:

```bash
# cat /sys/kernel/debug/tracing/events/sched/sched_switch/format
name: sched_switch
ID: 323
format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:char prev_comm[16];	offset:8;	size:16;	signed:1;
	field:pid_t prev_pid;	offset:24;	size:4;	signed:1;
	field:int prev_prio;	offset:28;	size:4;	signed:1;
	field:long prev_state;	offset:32;	size:8;	signed:1;
	field:char next_comm[16];	offset:40;	size:16;	signed:1;
	field:pid_t next_pid;	offset:56;	size:4;	signed:1;
	field:int next_prio;	offset:60;	size:4;	signed:1;

print fmt: "prev_comm=%s prev_pid=%d prev_prio=%d prev_state=%s%s ==> next_comm=%s next_pid=%d next_prio=%d", REC->prev_comm, REC->prev_pid, REC->prev_prio, (REC->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1)) ? __print_flags(REC->prev_state & ((((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) - 1), "|", { 0x0001, "S" }, { 0x0002, "D" }, { 0x0004, "T" }, { 0x0008, "t" }, { 0x0010, "X" }, { 0x0020, "Z" }, { 0x0040, "P" }, { 0x0080, "I" }) : "R", REC->prev_state & (((0x0000 | 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0010 | 0x0020 | 0x0040) + 1) << 1) ? "+" : "", REC->next_comm, REC->next_pid, REC->next_prio

```

From [LWN article part 1](#LWN_trace_event)

```
 To accomplish that, the TRACE_EVENT() macro is broken into six components, which correspond to the parameters of the macro:

   TRACE_EVENT(name, proto, args, struct, assign, print)

    name - the name of the tracepoint to be created.

    prototype - the prototype for the tracepoint callbacks

    args - the arguments that match the prototype.

    struct - the structure that a tracer could use (but is not required to) to store the data passed into the tracepoint.

    assign - the C-like way to assign the data to the structure.

    print - the way to output the structure in human readable ASCII format. 
```

Basic idea:
* add tracepoint at important parts e.g. in the scheduler code
* define callback function with arguments to trace
* add NOPs at compile time
* change NOPs to JMP to tracepoint trampoline at runtime if switched on

## Structure for example tracepoint sched:sched_switch: 

* TRACE_EVENT() macros defined in [`include/linux/tracepoint.h`](#tracepoint.h)

* `#define TRACE_SYSTEM sched` and `TRACE_EVENT(sched_switch,...)` macro used in [`include/trace/events/sched.h`](#sched.h) which will become the tracepoint `trace_sched_switch` (+ define_trace.h)

* `kernel/sched/core.c` includes `trace/events/sched.h` and calls [`trace_sched_switch`](#core.c)

* `trace_sched_switch` is the function (probe) the tracepoint calls and is "registered" via [`register_trace_sched_switch(probe_sched_switch, NULL);`](#trace_sched_switch.c)  in `kernel/trace/trace_sched_switch.c` with `probe_sched_switch` being the function which calls `tracing_record_taskinfo_sched_switch` in `kernel/trace/trace.c` to save prev/next command and pids.

* the print part of the TRACE_EVENT() macro creates the format file  /sys/kernel/debug/tracing/events/sched/sched_switch/format


See and/or compile the silly module mentioned above from [LWN part3](#LWN_trace_event3) for an easy example how a tracepoint is implemented.


## Links:

1. <a name="doc-tracepoints"></a> Kernel Documentation/trace/tracepoints.rst (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/tracepoints.rst)
1. <a name="LWN_trace_event"></a> "Using the TRACE_EVENT() macro (Part 1)" https://lwn.net/Articles/379903/
1. <a name="LWN_trace_event2"></a> "Using the TRACE_EVENT() macro (Part 2)" https://lwn.net/Articles/381064/
1. <a name="LWN_trace_event3"></a> "Using the TRACE_EVENT() macro (Part 3)" https://lwn.net/Articles/383362/ 
1. <a name="tracepoint.h"></a> "tracepoint.h" Linux Kernel: include/linux/tracepoint.h https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/linux/tracepoint.h#L413
1. <a name="sched.h"></a> `include/trace/events/sched.h` https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/trace/events/sched.h#L138
1. <a name="core.c"></a> `kernel/sched/core.c` https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/kernel/sched/core.c#L4077
1. <a name="trace_sched_switch.c"></a> `kernel/trace/trace_sched_switch.c` https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/kernel/trace/trace_sched_switch.c#L68


