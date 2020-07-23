tracing Part I subsection 1:  function tracer a.k.a. ftrace framework
================================================================================

files:
- ./kernel/trace/ftrace.c
- ./arch/x86/kernel/ftrace.c
- ./arch/x86/kernel/ftrace_64.S

Since 2008 2.6.27, ftrace histogram triggers 2016,2017

A [LWN article fromm 2008](#lwn0) describes ftraces the following:


> The ftrace (which stands for "function tracer") framework is one of the many improvements to come out of the realtime effort. Unlike SystemTap, it does not attempt to be a comprehensive, scriptable facility; ftrace is much more oriented toward simplicity. There is a set of virtual files in a debugfs directory which can be used to enable specific tracers and see the results. The function tracer after which ftrace is named simply outputs each function called in the kernel as it happens. Other tracers look at wakeup latency, events enabling and disabling interrupts and preemption, task switches, etc. As one might expect, the available information is best suited for developers working on improving realtime response in Linux. The ftrace framework makes it easy to add new tracers, though, so chances are good that other types of events will be added as developers think of things they would like to look at. 


Ftrace started the "tracing" directory in the debugfs virtual filesystem. Most basic operation is to print every(!) called kernel function. It has control files and output files, see Part II ftrace for details.

Dynamic ftrace (CONFIG_DYNAMIC_FTRACE=y), the default on x86_64, uses the `mcount()` call produced when compiling with `gcc -pg` flag  or `__fentry__` instead of `mcount()`.

From [Documentation/trace/ftrace-design.rst](#ftrace-design)

```bash
echo 'main(){}' | gcc -x c -S -o - - -pg | grep mcount
	        call    mcount
```
See section `HAVE_FUNCTION_TRACER` for details.

The call to mcount() in every function checks if you want to trace it and does the trace output, else just returns. If not in use the call will be replaced with a `NOP` (so you can selectively enable functions to trace). After compiling the kernel the `scripts/recordmcount` does this postprocessing. You will see the `NOP` "no-performance-penalty" trick again in the `tracepoint` section. 

See [part II section 1.](tracing-2_1.md) how to use the ftrace infra in `/sys/kernel/debug/tracing/`.


## Links:
1. <a name="lwn0"></a> LWN 2008: "Tracing: no shortage of options" (https://lwn.net/Articles/291091/)
1. <a name="lwn1"></a> LWN 2009: "A look at ftrace"      (https://lwn.net/Articles/322666/)
1. <a name="ftrace-design"></a> Kernel: Documentation/trace/ftrace-design.rst     (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/ftrace-design.rst)
1. <a name=""></a>     ()
1. <a name=""></a>     ()