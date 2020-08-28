tracing Part II: subsection "/sys/kernel/debug/tracing/"
================================================================================
## intro

First, it's the "easiest" way to use tracing because you don't need any tools. Normally "tracefs" is mounted on /sys/kernel/debug/tracing/ [see end of this document for notes on each file/directory](#tracing-files). One prominent use case is trace_printk() which uses the tracing-kernel-ring-buffer instead of flooding your console or serial port(for a long time). MagicKey SysRq+z also prints the trace ring buffer.

Second, other tools also leave their "traces" in `/sys/kernel/debug/tracing` either in automatically created new files or directories. It's *THE* central kernel tracing facility.

Different "tracers" all in one directory /sys/kernel/debug/tracing/:

* [function tracer, ftrace](#ftrace): activate with current_tracer file

* [tracepoints](#tracepoints): activate in events/ directory

* [kprobes](#tracepoints): activate with kprobe_events

* [uprobes](#tracepoints): activate with uprobe_events

* also BPF based tracers leave their traces in these directories and output in `/sys/kernel/debug/tracing/trace and trace_pipe` if they use `bpf_trace_printk()`.

Applicable to all:
```bash
cat trace # nonlbocking output of the trace ring buffer, can be read multiple times
cat trace_pipe # blocking output of the same buffer but clears it
echo 1 > tracing_on # enable
echo 0 > tracing_off # disable tracing, but tracing might still occur
```

Note: If you get `echo: write error: Device or resource busy` when activating different tracers, there might still be a `cat trace_pipe` running.


## function tracer <a name="ftrace"></a>
See [ftrace-Documentation](#documentation-ftrace) for all knobs and switches and all tracers in `available_tracers`. Will cover just function and function_graph.
Best used in a virtual machine: tracing a lot of functions (especially with stacktraces) adds noticeable amount of overhead.

```
# wc -l available_filter_functions
48742 available_filter_functions
# cat dyn_ftrace_total_info 
48742 pages:245 groups: 68  # same number as before
```

Basics:
```bash
# cat available_tracers
hwwlat blk mmiotrace function_graph wakeup_dl wakeup_rt wakeup function nop
# cat current_tracer
nop
# echo function > current_tracer # activate function tracer

# cat trace      # shows output of ring buffer
# cat trace_pipe # shows output and clears buffer after printing

# echo nop > current_tracer      # deactivate function tracer and clear ring buffer

```

```bash
#!/bin/bash 
# simple worklflow:
cd /sys/kernel/debug/tracing/
echo 0 > tracing_on     # disable ring-buffer
echo function > current_tracer
echo 1 > tracing_on
sleep 1
echo 0 > tracing_on
cp trace /tmp/tracing.save.$$
echo nop > current_tracer
```
Output:
```
 tracer: function
#
# entries-in-buffer/entries-written: 253915/259860   #P:8
#
#                              _-----=> irqs-off
#                             / _----=> need-resched
#                            | / _---=> hardirq/softirq
#                            || / _--=> preempt-depth
#                            ||| /     delay
#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION
#              | |       |   ||||       |         |
            bash-6806  [001] .... 83122.910145: mutex_unlock <-rb_simple_write
            bash-6806  [001] d.h. 83122.910165: __wake_up <-rb_wake_up_waiters
            bash-6806  [001] d.h. 83122.910166: __wake_up_common_lock <-__wake_up
            bash-6806  [001] d.h. 83122.910166: _raw_spin_lock_irqsave <-__wake_up_common_lock
            bash-6806  [001] d.h. 83122.910167: __wake_up_common <-__wake_up_common_lock
            bash-6806  [001] d.h. 83122.910167: _raw_spin_unlock_irqrestore <-__wake_up_common_lock
            bash-6806  [001] d.h. 83122.910168: irq_exit <-smp_irq_work_interrupt
            bash-6806  [001] .... 83122.910171: __fsnotify_parent <-vfs_write
            bash-6806  [001] .... 83122.910172: fsnotify <-vfs_write

```

cons: running the short script took ~6seconds, tracing every function lead to a tracing.save.$$ file with 410.000 lines on an almost idle system. See Linux Documentation/trace/ftrace.txt and events.txt on how to use filters, histograms, triggers and more (e.g. echo $$ > set_event_pid will just trace your current shell).

For C-code-style-graph: same script but `function_graph` instead of `function` tracer
```bash
#!/bin/bash 
# simple worklflow:
cd /sys/kernel/debug/tracing/
echo 0 > tracing_on     # disable ring-buffer
echo function_graph > current_tracer
echo 1 > tracing_on
sleep 1
echo 0 > tracing_on
cp trace /tmp/tracing.save.$$
echo nop > current_tracer
```

Output:
```
# tracer: function_graph
#
# CPU  DURATION                  FUNCTION CALLS
# |     |   |                     |   |   |   |
 2)   ==========> |
 2)               |  smp_reschedule_interrupt() {
 2)   0.274 us    |    scheduler_ipi();
 2)   2.982 us    |  }
 2)   <========== |
 2)               |  rcu_idle_exit() {
 2)   0.119 us    |    rcu_dynticks_eqs_exit();
 2)   0.394 us    |  }
 2)   0.118 us    |  arch_cpu_idle_exit();
 2)               |  tick_nohz_idle_exit() {
 2)   0.212 us    |    ktime_get();
 2)               |    update_ts_time_stats() {
 2)   0.127 us    |      nr_iowait_cpu();
 2)   0.382 us    |    }
 2)               |    __tick_nohz_idle_restart_tick() {
 2)   0.116 us    |      timer_clear_idle();
 2)   0.170 us    |      calc_load_nohz_stop();
 2)               |      hrtimer_cancel() {
 2)               |        hrtimer_try_to_cancel() {
 2)   0.186 us    |          hrtimer_active();
 2)               |          lock_hrtimer_base() {
 2)   0.126 us    |            _raw_spin_lock_irqsave();
 2)   0.352 us    |          }
 2)               |          __remove_hrtimer() {
 2)               |            hrtimer_force_reprogram() {
 2)               |              __hrtimer_get_next_event() {
 2)   0.125 us    |                __hrtimer_next_event_base();
 2)   0.357 us    |                __hrtimer_next_event_base();
 2)   0.826 us    |              }
 2)               |              tick_program_event() {
 2)               |                clockevents_program_event() {
 2)   0.142 us    |                  ktime_get();
 2)   1.945 us    |                  lapic_next_event();
 2)   2.492 us    |                }
 2)   2.799 us    |              }
 2)   3.973 us    |            }
 2)   4.482 us    |          }
 2)   0.136 us    |          _raw_spin_unlock_irqrestore();
 2)   5.771 us    |        }
 2)   6.000 us    |      }

```

```bash
echo $$ > set_ftrace_pid # just trace bash
echo function-fork > trace_options # and also child processes
echo hi > trace_markers # adds markers for use in scripts/programs
```
output:"            <...>-2929  [007] .... 84259.898198: tracing_mark_write: hi"

```bash
# cat trace|grep ls|wc -l # one simple ls of an empty directory:
34616
# echo func_stack_trace > trace_options # now ~1.7 million lines
[...]
           bash-5446  [007] d... 84766.914613: set_next_task_idle <-pick_next_task_idle
            bash-5446  [007] d... 84766.914619: <stack trace>
 => 0xffffffffc0a5b080
 => set_next_task_idle
 => pick_next_task_idle
 => __schedule
 => schedule
 => schedule_hrtimeout_range_clock
 => schedule_hrtimeout_range
 => poll_schedule_timeout.constprop.0
 => do_select
 => core_sys_select
 => do_pselect.constprop.0
 => __x64_sys_pselect6
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
 => 0xae066141ffffffff
 => 0x906ffffffff
 => 0x154600000001
 => ptep_set_access_flags
 => do_wp_page
 => 0x1000009e6
 => 0xadefc5f000001546
 => 0xade7f8a6ffffffff
[...]
```
Note: DO NOT clear the ftrace filter now. Enter `echo nofunc_stack_trace > trace_options` first. 

Use more filters to limit output (see available_filter_functions):

```bash
# echo 'futex_*' > set_ftrace_filter 
# cat set_ftrace_filter 
futex_top_waiter
futex_wait_queue_me
futex_wake
futex_wait_setup
futex_wait
futex_wait_restart
futex_lock_pi_atomic
futex_cleanup
futex_requeue
futex_wait_requeue_pi.constprop.0
futex_lock_pi
futex_exit_recursive
futex_exec_release
futex_exit_release

```

As explained in the LWN article ["Secrets of the Ftrace function tracer"](#ftracesecrets) using a command like `grep sched available_filter_functions > set_ftrace_filter` which adds hundreds of filter to the `set_ftrace_filter` file will result in millions of string comparisons and might take a second or two.


Now the output of a single `ls`, filtered on futex_* calls, is more reasonable: 8 stack traces, 89 lines with header:
```bash
# tracer: function
#
# entries-in-buffer/entries-written: 8/8   #P:8
#
#                              _-----=> irqs-off
#                             / _----=> need-resched
#                            | / _---=> hardirq/softirq
#                            || / _--=> preempt-depth
#                            ||| /     delay
#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION
#              | |       |   ||||       |         |
              ls-7413  [007] .... 86309.308197: futex_exec_release <-exec_mm_release
              ls-7413  [007] .... 86309.308226: <stack trace>
 => 0xffffffffc0a5b080
 => futex_exec_release
 => exec_mm_release
 => flush_old_exec
 => load_elf_binary
 => search_binary_handler
 => __do_execve_file.isra.0
 => __x64_sys_execve
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
 => 0x1c5b00000004
 => 0x13060000000d
 => 0xffffffffc0a5b080
 => lock_page_memcg
 => page_remove_rmap
 => unmap_page_range
 => unmap_single_vma
 => unmap_vmas
 [...]

```


## tracepoints a.k.a. events <a name="tracepoints"></a>
```bash
# wc -l available_events 
1518 available_events
```

Can use [Tracepoints(Documentation)](#documentation-tracepoints) often, confusingly, called "events":
in /sys/kernel/debug/tracing/events/
(Kernel options kprobes- and uprobes-events also mean they can be controlled by directories under /sys/kernel/debug/tracing/events/)

two ways of activating&deactivating them:
```bash
# echo sched_wakeup >> /sys/kernel/debug/tracing/set_event
# echo > /sys/kernel/debug/tracing/set_event
```
or 
```bash
# echo 1 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable
# echo 0 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable
```
Can dis-/enable whole subsystem with enable files in events/<subsystem>/.

Can filter on available parameters (see `format` files).

```
root@vbox:/sys/kernel/debug/tracing/events/sched/sched_process_exec# cat format
name: sched_process_exec
ID: 314
format:
	field:unsigned short common_type;	offset:0;	size:2;	signed:0;
	field:unsigned char common_flags;	offset:2;	size:1;	signed:0;
	field:unsigned char common_preempt_count;	offset:3;	size:1;	signed:0;
	field:int common_pid;	offset:4;	size:4;	signed:1;

	field:__data_loc char[] filename;	offset:8;	size:4;	signed:1;
	field:pid_t pid;	offset:12;	size:4;	signed:1;
	field:pid_t old_pid;	offset:16;	size:4;	signed:1;

print fmt: "filename=%s pid=%d old_pid=%d", __get_str(filename), REC->pid, REC->old_pid

```
Note: `print fmt` is default string appearing in trace output


Limit output to `wget`:
```
echo filename == "/usr/bin/wget" > /sys/kernel/debug/tracing/events/sched/sched_process_exec/filter
echo '!filename == "/usr/bin/wget"' > /sys/kernel/debug/tracing/events/sched/sched_process_exec/filter # disable with !
```
(note: filename could also be `./main` if started that way, it's not automatically the full absolute path)

```bash
echo nostacktrace     > /sys/kernel/debug/tracing/trace_options
echo nouserstacktrace > /sys/kernel/debug/tracing/trace_options
```

Output:
```
          <...>-9726  [005] .... 91860.339826: sched_process_exec: filename=/usr/bin/wget pid=9726 old_pid=9726
```
Output with stacktraces:
```bash
echo sym-userobj    > /sys/kernel/debug/tracing/trace_options
echo stacktrace     > /sys/kernel/debug/tracing/trace_options
echo userstacktrace > /sys/kernel/debug/tracing/trace_options
```
```
cat trace_pipe 
           <...>-9707  [005] .... 91787.004183: sched_process_exec: filename=/usr/bin/wget pid=9707 old_pid=9707
           <...>-9707  [005] .... 91787.004244: <stack trace>
 => __do_execve_file.isra.0
 => __x64_sys_execve
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
 => __x64_sys_exit_group
 => do_syscall_64
 => entry_SYSCALL_64_after_hwframe
 => 0x6146ffffffff
 => 0x154600000001
 => __fdget_pos
 => 0xad1e6ae0e623e
 => 0x1e1c00000001
           <...>-9707  [005] .... 91787.004245: <user stack trace>
 => /usr/lib/x86_64-linux-gnu/ld-2.30.so[+0x100]


```
(note: userstacktraces might not show because most programs are compiled with omit-frame-pointer).


Trigger files to activate other actions like histograms if a tracepoints/event is hit.
(cat trigger file to see available triggers)

```bash
# wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.6.7.tar.xz
# echo 'hist:keys=len:sort=len' > /sys/kernel/debug/tracing/events/net/netif_receive_skb/trigger
# cat /sys/kernel/debug/tracing/events/net/netif_receive_skb/hist
{ len:        343 } hitcount:          4
{ len:        500 } hitcount:          3
{ len:        584 } hitcount:          1
{ len:        585 } hitcount:          1
{ len:        620 } hitcount:          1
{ len:        621 } hitcount:          2
{ len:       1428 } hitcount:          2
{ len:       1444 } hitcount:       9266
{ len:       1460 } hitcount:          5
{ len:       1699 } hitcount:          1
{ len:       2848 } hitcount:        103
{ len:       3516 } hitcount:          1
{ len:       4252 } hitcount:         35
{ len:       4300 } hitcount:          3
{ len:       4920 } hitcount:          1

```
Only for wget: event `sched_exec` triggers histogram:

```bash
echo 'hist:keys=len:sort=len:pause' > /sys/kernel/debug/tracing/events/net/netif_receive_skb/trigger
echo 'enable_hist:net:netif_receive_skb if filename=="/usr/bin/wget"' > /sys/kernel/debug/tracing/events/sched/sched_process_exec/trigger
echo 'disable_hist:net:netif_receive_skb if comm=="/usr/bin/wget"' > /sys/kernel/debug/tracing/events/sched/sched_process_exit/trigger
```



## kprobes <a name="kprobes"></a>

Can use [kprobes(Documentation)](#documentation-kprobes):
in /sys/kernel/debug/tracing/events/kprobes
when active.
```bash
# echo 'p:myprobe do_sys_open +0(%si):string ' > kprobe_events && \
echo 1 > events/kprobes/myprobe/enable
# cat trace

      irqbalance-738   [004] .... 76222.463748: myprobe: (do_sys_open+0x0/0x80) arg1="/proc/irq/14/smp_affinity"
      irqbalance-738   [004] .... 76222.463755: myprobe: (do_sys_open+0x0/0x80) arg1="/proc/irq/15/smp_affinity"
          vminfo-1115  [001] .... 76227.360101: myprobe: (do_sys_open+0x0/0x80) arg1="/var/run/utmp"
     dbus-daemon-719   [001] .... 76227.360613: myprobe: (do_sys_open+0x0/0x80) arg1="/usr/local/share/dbus-1/system-services"
# echo 0 > events/kprobes/myprobe/enable
# echo > kprobe_events # clear

```

## uprobes <a name="uprobes"></a>

Can use [uprobes](#documentation-uprobes):
in /sys/kernel/debug/tracing/events/uprobes
when active.

Example:
```bash
# objdump -T /bin/bash|grep -w readline
00000000000ad900 g    DF .text	0000000000000088  Base        readline
#          ^^^^^
echo 'r /bin/bash:0xad900 +0($retval):string' > uprobe_events
echo 1 > events/uprobes/p_bash_0xad900/enable
# cat trace

# tracer: nop
#
# entries-in-buffer/entries-written: 1/1   #P:8
#
#                              _-----=> irqs-off
#                             / _----=> need-resched
#                            | / _---=> hardirq/softirq
#                            || / _--=> preempt-depth
#                            ||| /     delay
#           TASK-PID   CPU#  ||||    TIMESTAMP  FUNCTION
#              | |       |   ||||       |         |
            bash-10940 [004] d... 184464.361286: p_bash_0xad900: (0x557078fe1759 <- 0x55707905c900) arg1="cat trace"

# echo > uprobe_events # clear
```


[Function profiling](#ftracesecrets) is also possible. Copied from LWN Ftrace: "Secrets of the Ftrace function tracer" (https://lwn.net/Articles/370423/):


```
   [tracing]# echo nop > current_tracer
   [tracing]# echo 1 > function_profile_enabled
   [tracing]# cat trace_stat/function0 |head # for CPU0
     Function                               Hit    Time            Avg
     --------                               ---    ----            ---
     schedule                             22943    1994458706 us     86931.03 us 
     poll_schedule_timeout                 8683    1429165515 us     164593.5 us 
     schedule_hrtimeout_range              8638    1429155793 us     165449.8 us 
     sys_poll                             12366    875206110 us     70775.19 us 
     do_sys_poll                          12367    875136511 us     70763.84 us 
     compat_sys_select                     3395    527531945 us     155384.9 us 
     compat_core_sys_select                3395    527503300 us     155376.5 us 
     do_select                             3395    527477553 us     155368.9 us 

```

(Also interesting that ftrace once led to destroyed Intel e1000 adapters:(https://lwn.net/Articles/304105/). See for LWN-ftrace-list: (https://lwn.net/Kernel/Index/#Ftrace) )

## Links:

1. <a name="ftrace1"></a> LWN Ftrace: "Debugging the kernel using Ftrace - part 1" (https://lwn.net/Articles/365835/)
1. <a name="ftrace2"></a> LWN Ftrace: "Debugging the kernel using Ftrace - part 2" (https://lwn.net/Articles/366796/)

1. <a name="ftracesecrets"></a> LWN Ftrace: "Secrets of the Ftrace function tracer" (https://lwn.net/Articles/370423/)

1. <a name="documentation-ftrace"></a> Kernel ftrace Documentation (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/ftrace.rst)

1. <a name="documentation-tracepoints"></a> Kernel Tracepoints a.k.a. events Documentation (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/events.rst)

1. <a name="documentation-kprobes"></a> Kernel Kprobes Documentation (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/kprobetrace.rst)

1. <a name="documentation-uprobes"></a> Kernel Uprobes Documentation (https://github.com/torvalds/linux/blob/7111951b8d4973bda27ff663f2cf18b663d15b48/Documentation/trace/uprobetracer.rst)


## tracing files <a name="tracing-files"></a>

or cat /sys/kernel/debug/tracing/README for MINI-HOWTO.

file | description
-- | --
available_events|	lists every tracepoint
available_filter_functions|	list of every function tracer function
available_tracers|	list of available tracers: function, function_graph, hwlat,...
buffer_percent|	
buffer_size_kb|	per-cpu buffer size if equal on every cpu
buffer_total_size_kb|	total
current_tracer|	currently active tracer
dynamic_events|	file to activate different dynamic (on/off) tracers
dyn_ftrace_total_info|	
enabled_functions|	debug: active ftrace functions
error_log|	prints e.g. parsing errors
**events**|	directory for tracepoints, and kprobes, uprobes when active
free_buffer|	
function_profile_enabled|	
**hwlat_detector**|	
**instances**|	directory for custom `instances` with almost same files as .
kprobe_events|	activate kprobes which will be listed in events/
kprobe_profile|	kprobes hits 
max_graph_depth|	function_graph control file
**options**|	directory: different options for different tracers, might show options only if active
**per_cpu**|	directory: per-cpu control, trace files
printk_formats|	
README|	** MUST READ: MINI-HOWTO**
saved_cmdlines|	PID - cmdline-string cache
saved_cmdlines_size| default: 128 strings
saved_tgids|	default: off
set_event|	sets tracepoints for tracing
set_event_pid|	tracepoints only for specific PIDs
set_ftrace_filter|	ftrace only set functions
set_ftrace_notrace|	don't ftrace
set_ftrace_pid|	only pid
set_graph_function|	set ftrace-function-graph
set_graph_notrace|	don't function-graph-trace
snapshot|	swap ring-buffer to new one: `snapshot` them
stack_max_size|	max depth
stack_trace|	"Shows the max stack trace when active", at every ftrace function call record stack-depth
stack_trace_filter|	stack_trace only listed ones
synthetic_events|	
timestamp_mode|	
trace|	non-blocking trace output, can be read multiple times
trace_clock| which `clock`	
trace_marker|	
trace_marker_raw|	
trace_options| 	different options for different tracers, might show options only if active
trace_pipe|	blocking trace output: cat once, delete contents from ring-buffer
trace_stat|	
tracing_cpumask|	
tracing_max_latency|	
tracing_on|	
tracing_thresh|	
uprobe_events|	activate uprobes
uprobe_profile|	hits/misses





