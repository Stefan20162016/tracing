tracing Part I subsection 2:  kprobes, uprobes
===============================================================================

implementation files:
- kernel/kprobes.c
- kernel/arch/x86/kernel/kprobes/*.c


kprobes (kernel 2.6.9 or 2.6.10 in 2004), uprobes (kernel 3.5 in 2012) work in the same, dynamic way. They save and replace the function to trace with a breakpoint and the breakpoint handler creates the tracing event and single-steps through the saved instructions and resumes after the breakpoint. Compared to tracepoints the function names are more unstable and might change more frequently than the "strategically" placed tracepoints.

kprobes can be used via 

- a kernel module and `register_kprobe()` (see samples/kprobes; compile via kernel hacking -> sample kernel code)
- /sys/kernel/debug/tracing/dynamic_events or /sys/kernel/debug/tracing/kprobe_events. See Part II or [kprobetrace.rst](#kprobetrace)
- kernel cmdline bootstring `kprobe_event=`
- perf_event_open() via `perf` tool or via bpf tools

They can trace almost all functions except blacklisted ones, e.g. kprobe itself (NOKPROBE_SYMBOL() macro). See `cat /sys/kernel/debug/kprobes/blacklist`.

From Documentation/kprobes.txt:

> The register_*probe functions will return -EINVAL if you attempt
> to install a probe in the code that implements Kprobes (mostly
> kernel/kprobes.c and ``arch/*/kernel/kprobes.c``, but also functions such
> as do_page_fault and notifier_call_chain).

 Instead of the `int3` breakpoint a `jmp` optimization might be possible. The kprobe starts as `int3` and if the kernel verifies it's safe it gets replaced with a `jmp` ([see Documentation/kprobes.txt](#kprobes)). The whole optimization and un-optimization process at startup/teardown adds more overhead to kprobes compared to tracepoints. It can be turned off via `sysctl -w debug.kprobes_optimization=n`.


kretprobes: k-return-probes work similar. At function entry the return address is changed to a trampoline function which records the events and than jumpes back to the original return address. A combined kprobe+kretprobe is almost as fast as a single kretprobe (see Documentation/kprobes.txt -> "Probe Overhead").

Use `cat /sys/kernel/debug/kprobes/list` to list currently installed kprobes on your system. E.g. for the kprobe-module from the kernel samples directory:

```bash
# cat /sys/kernel/debug/kprobes/list 
ffffffff96499cf0  k  _do_fork+0x0    [FTRACE]
```


## Links:

1. <a name="kprobes"></a> Documentation/kprobes.txt (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/kprobes.txt)
1. <a name="kprobetrace"></a> Documentation/trace/kprobetrace.rst      (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/kprobetrace.rst)
