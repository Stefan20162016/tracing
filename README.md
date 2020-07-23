# Kernel Tracing

(based on kernel 5.6.6)

There are a few (complementary) tracing tools, purely built-in (Ftrace: function tracer framework) in virtual debugfs which can be accessed with `cat/echo` or built-in with external tool and shipped with the kernel: `perf`, or combined with the in-kernel BPF capabilities via the `bpf` syscall, which tools like `bpftrace` and `BCC` use which themselves use LLVM/Clang to produce BPF bytecode.

 Overall quite confusing because every tool can use most or a huge subset of the underlying tracing functionality (function tracer, tracepoints, kprobes, kretprobes, uprobes, uretprobes, USDT). As a quick hint, `bpftrace` is easy to learn (simple syntax) and quite powerful because it can use in-kernel aggregation via BPF like histograms, stacks and more. See the short examples in the [Introduction](tracing-intro.md) and [bpftrace section](tracing-2_2.md).

This is more a practical tutorial and the sections explain tracing functionality and than how to use them with short code snippets. See kernel Documentation/trace/* and/or kernel code for exact implementation.


1. [Introduction](tracing-intro.md)

* Theoretical Part I: tracing infrastructure

1. [function tracer / ftrace](tracing-1_1.md)
1. [tracepoints](tracing-1_2.md)
1. [kprobes, kretprobes, uprobes, uretprobes](tracing-1_3.md)
1. [USDT](tracing-1_4.md)
1. [perf_events()](tracing-1_5.md)

* Practical Part II: tracing tools

1. [ftrace: /sys/kernel/debug/tracing/](tracing-2_1.md)
1. [eBPF](tracing-2_2.md)
1. [bpftrace](tracing-2_3.md)
1. [BCC](tracing-2_4.md)
1. [./perf](tracing-2_5.md)
1. [LTTng SystemTap?](tracing-2_6.md)


BPF features added to kernel versions(from bpftrace/INSTALL.md):
- 4.1 - kprobes
- 4.3 - uprobes
- 4.6 - stack traces, count and hist builtins (use PERCPU maps for accuracy and efficiency)
- 4.7 - tracepoints
- 4.9 - timers/profiling



