#!/bin/bash

set -e

# BoringSSL build as described in the Security Policy for BoringCrypto module (2018-10-25):
# https://csrc.nist.gov/CSRC/media/projects/cryptographic-module-validation-program/documents/security-policies/140sp3318.pdf

# This works only on Linux-x86_64.
if [[ `uname` != "Linux" || `uname -m` != "x86_64" ]]; then
  echo "ERROR: BoringSSL FIPS is currently supported only on Linux-x86_64."
  exit 1
fi

# BoringSSL commit and checksum from the Security Policy:
COMMIT=66005f41fbc3529ffe8d007708756720529da20d # fips-20180730
SHA256=b12ad676ee533824f698741bd127f6fbc82c46344398a6d78d25e62c6c418c73

curl -sLO https://commondatastorage.googleapis.com/chromium-boringssl-docs/fips/boringssl-"$COMMIT".tar.xz \
  && echo "$SHA256" boringssl-"$COMMIT".tar.xz | sha256sum --check
tar xf boringssl-"$COMMIT".tar.xz

# Build tools requirements:
# - Clang compiler version 6.0.1 (http://releases.llvm.org/download.html)
# - Go programming language version 1.10.3 (https://golang.org/dl/)
# - Ninja build system version 1.8.2 (https://github.com/ninja-build/ninja/releases)

# Override $PATH for build tools, to avoid picking up anything else.
export PATH="$(dirname `which cmake`):/usr/bin:/bin"

# Clang 6.0.1
VERSION=6.0.1
SHA256=7ea204ecd78c39154d72dfc0d4a79f7cce1b2264da2551bb2eef10e266d54d91
PLATFORM="x86_64-linux-gnu-ubuntu-16.04"

curl -sLO https://releases.llvm.org/"$VERSION"/clang+llvm-"$VERSION"-"$PLATFORM".tar.xz \
  && echo "$SHA256" clang+llvm-"$VERSION"-"$PLATFORM".tar.xz | sha256sum --check
tar xf clang+llvm-"$VERSION"-"$PLATFORM".tar.xz

export HOME="$PWD"
printf "set(CMAKE_C_COMPILER \"clang\")\nset(CMAKE_CXX_COMPILER \"clang++\")\n" > ${HOME}/toolchain
export PATH="$PWD/clang+llvm-$VERSION-$PLATFORM/bin:$PATH"

if [[ `clang --version | head -1 | awk '{print $3}'` != "$VERSION" ]]; then
  echo "ERROR: Clang version doesn't match."
  exit 1
fi

# Go 1.10.3
VERSION=1.10.3
SHA256=fa1b0e45d3b647c252f51f5e1204aba049cde4af177ef9f2181f43004f901035
PLATFORM="linux-amd64"

curl -sLO https://dl.google.com/go/go"$VERSION"."$PLATFORM".tar.gz \
  && echo "$SHA256" go"$VERSION"."$PLATFORM".tar.gz | sha256sum --check
tar xf go"$VERSION"."$PLATFORM".tar.gz

export PATH="$PWD/go/bin:$PATH"

if [[ `go version | awk '{print $3}'` != "go$VERSION" ]]; then
  echo "ERROR: Go version doesn't match."
  exit 1
fi

# Ninja 1.8.2
VERSION=1.8.2
SHA256=d2fea9ff33b3ef353161ed906f260d565ca55b8ca0568fa07b1d2cab90a84a07
PLATFORM="linux"

curl -sLO https://github.com/ninja-build/ninja/releases/download/v"$VERSION"/ninja-"$PLATFORM".zip \
  && echo "$SHA256" ninja-"$PLATFORM".zip | sha256sum --check
unzip ninja-"$PLATFORM".zip

export PATH="$PWD:$PATH"

if [[ `ninja --version` != "$VERSION" ]]; then
  echo "ERROR: Ninja version doesn't match."
  exit 1
fi

# Build BoringSSL.
cd boringssl
mkdir build && cd build && cmake -GNinja -DCMAKE_TOOLCHAIN_FILE=${HOME}/toolchain -DFIPS=1 -DCMAKE_BUILD_TYPE=Release ..
ninja
ninja run_tests

# Verify correctness of the FIPS build.
if [[ `tool/bssl isfips` != "1" ]]; then
  echo "ERROR: BoringSSL tool didn't report FIPS build."
  exit 1
fi

# Install to $THIRDPARTY_BUILD.
cp -pR ../include/openssl "$THIRDPARTY_BUILD"/include/
cp -p  crypto/libcrypto.a "$THIRDPARTY_BUILD"/lib/
cp -p  ssl/libssl.a "$THIRDPARTY_BUILD"/lib/
