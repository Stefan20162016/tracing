tracing Part I subsection 2:  Tracepoints
================================================================================

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

"A function (probe) that you can provide at runtime" might be misleading and could mean that you can do that in a kernel module, major subsystem tracepoints like in `sched` are pretty hardcoded. You can turn them on/off at runtime.

Tracepoints are sorted by subsytem:eventname, following the example in the comments in [include/linux/tracepoint.h](#tracepoint.h) we'll look at `tracepoint:sched:sched_switch`. It's using the simpler TRACE_EVENT(...) macro mentioned at the end of https://github.com/torvalds/linux/blob/v5.6/Documentation/trace/tracepoints.rst and which is also explained in an [LWN Article series part1-3](#LWN_trace_event).


Basic idea:
* add tracepoint at important parts e.g. in the scheduler code
* define callback function with arguments to trace
* add NOPs at compile time
* jump to tracepoint trampoline at runtime if switched on

## Structure for example tracepoint sched:sched_switch: 

* TRACE_EVENT() macros defined in [`include/linux/tracepoint.h`](#tracepoint.h)
* `#define TRACE_SYSTEM sched` and `TRACE_EVENT(sched_switch,...)` macro used in [`include/trace/events/sched.h`](#sched.h) which will become the tracepoint `trace_sched_switch` (+ define_trace.h)
* `kernel/sched/core.c` includes `trace/events/sched.h` and calls [`trace_sched_switch`](#core.c)
* `trace_sched_switch` is the function (probe) the tracepoint calls and is "registered" via [`register_trace_sched_switch(probe_sched_switch, NULL);`](#trace_sched_switch.c)  in `kernel/trace/trace_sched_switch.c`



## Links:

1. <a name="LWN_trace_event"></a> "Using the TRACE_EVENT() macro (Part 1)" https://lwn.net/Articles/379903/
1. <a name="LWN_trace_event2"></a> "Using the TRACE_EVENT() macro (Part 2)" https://lwn.net/Articles/381064/
1. <a name="LWN_trace_event3"></a> "Using the TRACE_EVENT() macro (Part 3)" https://lwn.net/Articles/383362/ 
1. <a name="tracepoint.h"></a> Linux Kernel: include/linux/tracepoint.h https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/linux/tracepoint.h#L413
1. <a name="sched.h"></a> Linux Kernel: include/trace/events/sched.h https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/include/trace/events/sched.h#L138
1. <a name="core.c"></a> Linux Kernel: kernel/sched/core.c https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/kernel/sched/core.c#L4077
1. <a name="trace_sched_switch.c"></a> Linux Kernel: kernel/trace/trace_sched_switch.c https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/kernel/trace/trace_sched_switch.c#L68


