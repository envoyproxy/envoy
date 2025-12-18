#!/bin/bash

set -euo pipefail

uncomment.sh "$1" --comment -h \
--uncomment-macro OPENSSL_ASM_INCOMPATIBLE \
--uncomment-macro OPENSSL_NO_ASM \
--uncomment-macro OPENSSL_NO_THREADS_CORRUPT_MEMORY_AND_LEAK_SECRETS_IF_THREADED \
--uncomment-macro 'OPENSSL_\(X86_64\|X86\|AARCH64\|ARM\|MIPS\|MIPS64\|RISCV64\|PNACL\)' \
--uncomment-macro 'OPENSSL_\(APPLE\|MACOS\|IOS\)' \
--uncomment-macro 'OPENSSL_\(WINDOWS\|LINUX\|FUCHSIA\|TRUSTY\|ANDROID\|FREEBSD\)' \
--uncomment-macro 'OPENSSL_[ATM]SAN' \
--uncomment-macro  OPENSSL_64_BIT \
--uncomment-macro  OPENSSL_32_BIT 
