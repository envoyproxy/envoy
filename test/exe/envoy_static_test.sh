#!/bin/bash

if [[ `uname` == "Darwin" ]]; then
  echo "macOS doesn't support statically linked binaries, skipping."
  exit 0
fi

# We can't rely on the exit code alone, since lld fails for statically linked binaries.
DYNLIBS=$(ldd source/exe/envoy-static 2>&1)
if [[ $? != 0 && ! "${DYNLIBS}" =~ "not a dynamic executable" ]]; then
  echo "${DYNLIBS}"
  exit 1
fi

if [[ ${DYNLIBS} =~ "libc++" ]]; then
  echo "libc++ is dynamically linked:"
  echo "${DYNLIBS}"
  exit 1
fi

if [[ ${DYNLIBS} =~ "libstdc++" || ${DYNLIBS} =~ "libgcc" ]]; then
  echo "libstdc++ and/or libgcc are dynamically linked:"
  echo "${DYNLIBS}"
  exit 1
fi
