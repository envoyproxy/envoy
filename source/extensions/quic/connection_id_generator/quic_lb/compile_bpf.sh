#!/bin/bash

# Usage: ./compile_bpf.sh <path to bpf_asm>
# bpf_asm can be found at https://github.com/torvalds/linux/blob/master/tools/bpf/bpf_asm.c

$1 -c route.bpf | sed -e 's/0xabcdefff/concurrency/'
