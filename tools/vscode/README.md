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
