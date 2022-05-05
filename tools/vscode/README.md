# Tools for VSCode

This directory contains tools which is useful for developers using VSCode.

## Recommended VSCode setup

It is recommended to use [devcontainer](../../.devcontainer/README.md), or setting up an equivalent
environment. Recommended extensions and settings are listed in
[devcontainer.json](../../.devcontainer/devcontainer.json).

## Refresh compilation database

`tools/vscode/refresh_compdb.sh` is a script to refresh compilation database, it may take a while
to generate all dependencies for code completion, such as protobuf generated codes, external dependencies.
If you changed proto definition, or changed any bazel structure, rerun this to get code completion
correctly.

Note that it is recommended to disable VSCode Microsoft C/C++ extension and use `vscode-clangd` instead for
C/C++ code completion.

For Chinese developers, you need to set `http_proxy` `https_proxy` `all_proxy` in advance.

### Tips for proxmox users

If you see the following error message:
```
+ cmake -DCMAKE_AR=/usr/bin/ar '-DCMAKE_SHARED_LINKER_FLAGS=-shared -fuse-ld=/opt/llvm/bin/ld.lld -Wl,-no-as-needed -Wl,-z,relro,-z,now -B/opt/llvm/bin -lm -fuse-ld=lld -l:libstdc++.a' '-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=/opt/llvm/bin/ld.lld -Wl,-no-as-needed -Wl,-z,relro,-z,now -B/opt/llvm/bin -lm -fuse-ld=lld -l:libstdc++.a' -DBOOST_ROOT=/build/envoy-compdb/_bazel_vscode/2d35de14639eaad1ac7060a4dd7e3351/sandbox/processwrapper-sandbox/1270/execroot/envoy/external/org_boost -DBUILD_EXAMPLES=off -DCMAKE_INSTALL_LIBDIR=lib -DRAGEL=/build/envoy-compdb/_bazel_vscode/2d35de14639eaad1ac7060a4dd7e3351/sandbox/processwrapper-sandbox/1270/execroot/envoy/bazel-out/k8-fastbuild/bin/contrib/hyperscan/matching/input_matchers/source/hyperscan.ext_build_deps/ragel/bin/ragel -DCMAKE_BUILD_TYPE=Bazel -DCMAKE_INSTALL_PREFIX=/build/envoy-compdb/_bazel_vscode/2d35de14639eaad1ac7060a4dd7e3351/sandbox/processwrapper-sandbox/1270/execroot/envoy/bazel-out/k8-fastbuild/bin/contrib/hyperscan/matching/input_matchers/source/hyperscan -DCMAKE_PREFIX_PATH=/build/envoy-compdb/_bazel_vscode/2d35de14639eaad1ac7060a4dd7e3351/sandbox/processwrapper-sandbox/1270/execroot/envoy/bazel-out/k8-fastbuild/bin/contrib/hyperscan/matching/input_matchers/source/hyperscan.ext_build_deps -DCMAKE_RANLIB= -DCMAKE_MAKE_PROGRAM=ninja -G Ninja /build/envoy-compdb/_bazel_vscode/2d35de14639eaad1ac7060a4dd7e3351/sandbox/processwrapper-sandbox/1270/execroot/envoy/external/io_hyperscan
CMake Deprecation Warning at CMakeLists.txt:1 (cmake_minimum_required):
  Compatibility with CMake < 2.8.12 will be removed from a future version of
  CMake.

  Update the VERSION argument <min> value or use a ...<max> suffix to tell
  CMake that the project does not need compatibility with older versions.

......

-- Performing Test HAVE_SSSE3
-- Performing Test HAVE_SSSE3 - Failed
-- Performing Test HAVE_AVX2
-- Performing Test HAVE_AVX2 - Failed
-- Performing Test HAVE_AVX512
-- Performing Test HAVE_AVX512 - Failed
-- Building without AVX2 support
-- Building without AVX512 support
CMake Error at cmake/arch.cmake:108 (message):
  A minimum of SSSE3 compiler support is required
Call Stack (most recent call first):
  CMakeLists.txt:340 (include)

......
```
Please check the cpu info of your virtual machine (e.g., ssse3):

`cat /proc/cpuinfo | grep ssse3`

If there is no output from this command, change the default cpu type `kvm64` to `max`.

## Generate debug config

`tools/vscode/generate_debug_config.py` is a script to generate VSCode debug config in `.vscode/launch.json`.
The generated config will be named `<debugger type> <bazel target>`.

For example:
```
tools/vscode/generate_debug_config.py //source/exe:envoy-static --args "-c envoy.yaml"
```

Generates an entry named `gdb //source/exe:envoy-static` for GDB in `launch.json`. It can be
used to generate config for tests also.

The generated `gdb` config are compatible with [Native Debug](https://marketplace.visualstudio.com/items?itemName=webfreak.debug) extension,
`lldb` config are compatible with [VSCode LLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) extension.
