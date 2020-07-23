tracing Part II: subsection "perf"
================================================================================

kernel 2.6.31 ?


https://perf.wiki.kernel.org/index.php/Tutorial

http://web.eece.maine.edu/~vweaver/projects/perf_events/



from (#lkml)
```
x86 Intel Haswell LBR call stack support: this is a new Haswell 
   feature that allows the hardware recording of call chains, plus 
   tooling support.  To activate this feature you have to enable it 
   via the new 'lbr' call-graph recording option:

     perf record --call-graph lbr
     perf report

   or:

     perf top --call-graph lbr

   This hardware feature is a lot faster than stack walk or dwarf
   based unwinding, but has some limitations:

    - It reuses the current LBR facility, so LBR call stack and branch
      record can not be enabled at the same time.

    - It is only available for user-space callchains.
```




## Links:

1. <a name="lkml"></a> "Subject: [GIT PULL] perf updates for v4.1 share " (https://lkml.org/lkml/2015/4/14/232)

1. <a name=""></a> description      ()
1. <a name=""></a> description      ()
1. <a name=""></a> description      ()
