# Envoy Clang Libtooling developer tools

## Overview

A number of tools live in this directory that are intended for use by Envoy
developers (and potentially CI). These are host tools and should not be linked
into the Envoy target. They are based around Clang's
[libtooling](https://clang.llvm.org/docs/LibTooling.html) libraries, a C++
framework for writing Clang tools in the style of `clang-format` and
`clang-check`.

## Building and running

To build tools in this tree, a Clang binary install must be available. If you
are building Envoy with `clang`, this should already be true of your system. You
can find prebuilt binary releases of Clang at https://releases.llvm.org. You
will need the Clang version used by Envoy in CI (currently clang-9.0).

To build a tool, set the following environment variable:

```console
export LLVM_CONFIG=<path to clang installation>/bin/llvm-config
```

Assuming that `CC` and `CXX` already point at Clang, you should be able to build
with:

```console
bazel build @envoy_dev//clang_tools/syntax_only
```

To run `libtooling` based tools against Envoy, you will need to first generate a
compilation database, which tells the tool how to take a source file and locate
its various dependencies. The `tools/gen_compilation_database.py` script
generates this and also does setup of the Bazel cache paths to allow external
dependencies to be located:

```console
tools/gen_compilation_database.py --run_bazel_build --include_headers
```

Finally, the tool can be run against source files in the Envoy tree:

```console
bazel-bin/external/envoy_dev/clang_tools/syntax_only/syntax_only \
  source/common/common/logger.cc
```

## Adding a new Envoy libtooling based tool

Follow the example at `tools/clang_tools/syntax_only`, based on the tutorial
example at https://clang.llvm.org/docs/LibTooling.html. Please use the
`envoy_clang_tools_cc_binary` Bazel macro for the tool, this disables use of
RTTI/exceptions and allows developer tools to be structurally excluded from the
build as needed.
