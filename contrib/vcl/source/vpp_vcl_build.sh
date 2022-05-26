#!/bin/bash

set -e

VPP_PATH=$1
DST_PATH=$2

# This works only on Linux.
if [[ $(uname) != "Linux" ]]; then
  echo "ERROR: VPP VCL is currently supported only on Linux"
  exit 1
fi

# Log cmake and ninja versions for later debugging
echo "Building VCL"
echo "running $(cmake --version | head -1)"
echo "ninja version $(ninja --version)"

# Build
pushd "${VPP_PATH}"
mkdir _vcl
cd _vcl
cmake -G Ninja ../src -DCMAKE_BUILD_TYPE:STRING=release
ninja -C . vppcom

mv CMakeFiles/vcl/libvppcom.a "${DST_PATH}"
mv CMakeFiles/vppinfra/libvppinfra.a "${DST_PATH}"
mv CMakeFiles/svm/libsvm.a "${DST_PATH}"
mv CMakeFiles/vlibmemory/libvlibmemoryclient.a "${DST_PATH}"
cp ../src/vcl/vppcom.h "${DST_PATH}"

popd
