#!/bin/bash

set -e
git clone https://gerrit.fd.io/r/vpp
BASEDIR=$(pwd)
cd vpp
git clean -fdx

# NOTE: Hard-coded to build the VPP debug libraries.
#       To build release libraries, use the following two lines instead:
#
#ARTIFACT_BASE=${BASEDIR}/vpp/build-root/install-vpp-native/vpp
#make vpp_uses_dpdk=no build-release
#
ARTIFACT_BASE=${BASEDIR}/vpp/build-root/install-vpp_debug-native/vpp
make vpp_uses_dpdk=no build

cd $ARTIFACT_BASE/include

find ./svm -name '*.h' | cpio -updm "$THIRDPARTY_BUILD"/include
find ./vlibmemory -name '*.h' | cpio -updm "$THIRDPARTY_BUILD"/include
find ./vppinfra -name '*.h' | cpio -updm "$THIRDPARTY_BUILD"/include
find ./vcl -name '*.h' | cpio -updm "$THIRDPARTY_BUILD"/include

cd $ARTIFACT_BASE/lib64
cp libsvm.a "$THIRDPARTY_BUILD"/lib
cp libvlib.a "$THIRDPARTY_BUILD"/lib
cp libvlibmemoryclient.a "$THIRDPARTY_BUILD"/lib
cp libvppinfra.a "$THIRDPARTY_BUILD"/lib
cp libvppcom.a "$THIRDPARTY_BUILD"/lib
