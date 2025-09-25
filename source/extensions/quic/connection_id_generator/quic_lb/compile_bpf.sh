#!/bin/bash

# Usage: ./compile_bpf.sh <path to bpf_asm>
# bpf_asm can be found at https://github.com/torvalds/linux/blob/master/tools/bpf/bpf_asm.c

$1 -c route.bpf | sed -e 's/0xabcdefff/concurrency/' | sed -e 's/0xfffff020/static_cast<uint32_t>(SKF_AD_OFF + SKF_AD_RXHASH)/'
