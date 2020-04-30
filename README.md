# Kernel Tracing

(base on kernel 5.6.6)

There are a few (complementary) tracing tools, purely built-in (Ftrace) or built-in with external tool but shipped with the kernel (perf), or combined with the in-kernel BPF capabilities (bpftrace, BCC). Overall quite confusing because every tool can use most or a subset of the underlying tracing functionality (function tracer, kprobes, kretprobes, uprobes, uretprobes, USDT). As a quick hint, bpftrace is easy to learn (simple syntax) and quite powerful (in-kernel aggregation using BPF). See the short examples in the [Introduction](tracing-1.md) and [bpftrace section](tracing-.md).

Sections explain tracing functionality and than how to use it. With short kernel code snippets. See kernel Documentation/trace/* and/or kernel code for exact implementation.



1. [Introduction](tracing-intro.md)

* Part I: tracing infrastructure
1. [function tracer / ftrace](tracing-.md)
1. [tracepoints](tracing-.md)
1. [kprobes, kretprobes, uprobes, uretprobes](tracing-.md)
1. [USDT](tracing-.md)
1. [perf_events()](tracing-.md)

* Part II: tracing tools
1. [/sys/kernel/debug/tracing/](tracing-2_1.md)
1. [perf_events(), ./perf](tracing-.md)
1. [LTTng SystemTap?](tracing-.md)
1. [bpftrace](tracing-.md)
1. [BCC](tracing-.md)




