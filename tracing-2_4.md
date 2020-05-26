tracing Part II: subsection "BCC"
================================================================================
## BCC: BPF Compiler Collection intro

Installation notes: best to compile from scratch for latest features BTF debugging support (e.g. source code interweaved in `bpftool prog dump xlated` output). BCC uses libbpf from your distribution or kernel or mirror at https://github.com/libbpf/libbpf or libbpf from BCC repo, so might easily end up with bcc tools which can't use all your kernel's bpf features.





