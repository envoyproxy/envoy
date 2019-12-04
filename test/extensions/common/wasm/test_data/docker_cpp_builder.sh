#!/bin/bash
source /root/emsdk/emsdk_env.sh
export PATH=/usr/local/bin:$PATH
make -j -f Makefile.docker_cpp_builder
