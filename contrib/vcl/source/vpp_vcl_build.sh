#!/bin/bash

set -e

VPP_PATH=$1
DST_PATH=$2

DEBUG_VPP=

# This works only on Linux.
if [[ $(uname) != "Linux" ]]; then
  echo "ERROR: VPP VCL is currently supported only on Linux"
  exit 1
fi

NINJA_ARGS=()

# TODO(phlax): quieten this properly once ninja is updated to 1.11+
#    https://github.com/ninja-build/ninja/commit/0a632d11eb1e8b7becbd9756c3e3439777924f6d
#    https://discourse.cmake.org/t/making-ninja-build-output-silent/4439
if [[ -n "$DEBUG_VPP" ]]; then
    # Log cmake and ninja versions for later debugging
    echo "Building VCL"
    echo "running $(cmake --version | head -1)"
    echo "ninja version $(ninja --version)"
else
    :
    # NINJA_ARGS+=(--quiet)
fi

# Build
pushd "${VPP_PATH}"
mkdir _vcl
cd _vcl

if [[ -n "$DEBUG_VPP" ]]; then
    cmake -G Ninja ../src -DCMAKE_BUILD_TYPE:STRING=release
    ninja "${NINJA_ARGS[@]}" -C . vppcom
else
    # TODO(phlax): remove redirection once ninja is updated to 1.11+
    cmake -G Ninja ../src -DCMAKE_BUILD_TYPE:STRING=release &> /dev/null
    ninja "${NINJA_ARGS[@]}" -C . vppcom > /dev/null
fi

mv CMakeFiles/vcl/libvppcom.a "${DST_PATH}"
mv CMakeFiles/vppinfra/libvppinfra.a "${DST_PATH}"
mv CMakeFiles/svm/libsvm.a "${DST_PATH}"
mv CMakeFiles/vlibmemory/libvlibmemoryclient.a "${DST_PATH}"
cp ../src/vcl/vppcom.h "${DST_PATH}"

popd
