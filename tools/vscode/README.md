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

For developers in China, you might need to set `http_proxy`, `https_proxy`, `all_proxy` in advance.

### Tips for proxmox users

If you see the following error message:
```
ERROR: /workspaces/envoy/contrib/hyperscan/matching/input_matchers/source/BUILD:21:12: Foreign Cc - CMake: Building hyperscan failed: (Exit 1): bash failed: error executing command /bin/bash -c bazel-out/k8-fastbuild/bin/contrib/hyperscan/matching/input_matchers/source/hyperscan_foreign_cc/wrapper_build_script.sh

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
