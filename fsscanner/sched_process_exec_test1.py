#!/usr/bin/python3
# curtask->mm->exe_file->f_inode->i_ino 
# path-lookup.txt: https://lwn.net/Articles/419826/



import csv
import os
import numpy as np
import warnings
warnings.filterwarnings(action='ignore',message='.*overflow*',category=RuntimeWarning)

from bcc import BPF

b = BPF(text="""
#include <uapi/linux/bpf.h>
#include <linux/dcache.h>
#include <linux/err.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/path.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <uapi/linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs.h>

struct data_t {
    u64 ts;
    u32 pid;
};

BPF_PERF_OUTPUT(events);

//int kprobe__do_nanosleep(void *ctx){
    
TRACEPOINT_PROBE(sched, sched_process_exec) { 
    struct data_t data = {};
    data.pid = bpf_get_current_pid_tgid();
    
    struct task_struct *curtask = (struct task_struct *) bpf_get_current_task();
    //inode= (*curtask->mm->exe_file->f_inode).i_ino;
    struct mm_struct *mm = curtask->mm;
    struct file *exe_file = mm->exe_file;
    struct inode *inode = exe_file->f_inode;
    
    data.ts = inode->i_ino;
    
    events.perf_submit(args, &data, sizeof(data));
    return 0;
};

""")
#u64 i = inode->i_ino;
#data.ts = bpf_ktime_get_ns() / 1000;

inode_dict = {}

with open('output', newline='') as csvfile:
    print('reading _*OLD*_ output file')
    csvreader = csv.DictReader(csvfile,delimiter=';')
    for row in csvreader:
        #rint(row['inode'], row['hash'])
        inode_dict[int(row['inode'])]=[row['hash'], row['file'] ]

def elf_hash(filename):
    print('in elf_hash for file: ' , filename)
    fd = os.open(filename, os.O_RDONLY)
    string1 = os.read(fd, 128)
    hash1=np.int32(5381)
    for i in range(4,128):
        tmp=np.int32()
        #print('i: ', i, ' tmp: ', tmp)
        tmp=np.left_shift(hash1, np.int32(5)) + hash1
        #print('i: ', i, ' tmp: ', tmp)
        hash1 = np.int32(tmp) + np.int8(string1[i])
        #print('i: ', i, ' hash: ', hash1)

    inthash1=int(hash1)
    return inthash1



def print_event(cpu, data, size):
    event = b["events"].event(data)
    print("%u %u" % (event.ts, event.pid))
    #print(type(event.ts))
    inode=event.ts
    hash_file=inode_dict.get(inode)
    if(hash_file != None):
        print('[hash, file] from ./output ', hash_file)
        new_hash = elf_hash(hash_file[1])
        intsavedhash = int(hash_file[0])
        print('newhash: ', new_hash, ' savedhash: ', intsavedhash)
        if(new_hash != intsavedhash):
            print('************** NOT EQUAL *****************')
        else:
            print('equal')

    else:
        print('does not exist in inode_dict: ', inode)

    print()
    



b["events"].open_perf_buffer(print_event)

while 1:
    try:
        b.perf_buffer_poll()
    except KeyboardInterrupt:
        exit()




