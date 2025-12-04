#!/bin/bash

# Usage: ./compile_bpf.sh <path to bpf_asm>
# bpf_asm can be found at https://github.com/torvalds/linux/blob/master/tools/bpf/bpf_asm.c

# `concurrency` is a variable in the Envoy code this is used in, and must be
# inserted into the program to correctly compute the correct worker. A placeholder
# value is set in the BPF source code to make this substitution easier.
$1 -c route.bpf | sed -e 's/0xabcdefff/concurrency/'
