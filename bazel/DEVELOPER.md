# Developer guide for writing Envoy Bazel rules

When adding or maintaining Envoy binary, library and test targets, it's
necessary to write or modify Bazel `BUILD` files. In general, each directory has
a `BUILD` file covering the source files contained immediately in the directory.

Some guidelines for defining new targets using the [custom Envoy build
rules](../bazel/envoy_build_system.bzl) are provided below. The [Bazel BUILD
Encyclopedia](https://bazel.build/versions/master/docs/be/overview.html)
provides further details regarding the underlying rules.

## Style guide

The [BUILD file style
guide](https://bazel.build/versions/master/docs/skylark/build-style.html) is the
canonical style reference. The
[buildifier](https://github.com/bazelbuild/buildifier) tool automatically
enforces these guidelines. In addition, within the `BUILD` file, targets should
be sorted alphabetically by their `name` attribute.

## Adding files to the Envoy build

All modules that make up the Envoy binary are statically linked at compile time.
Many of the modules within Envoy have a pure virtual interface living in
[`include/envoy`](../include/envoy), implementation sources in
[`source`](../source), mocks in [`test/mocks`](../test/mocks) and
unit/integration tests in [`test`](../test). The relevant `BUILD` files will
require updating or to be added in these locations as you extend Envoy.

As an example, consider adding the following interface in `include/envoy/foo/bar.h`:

```c++
#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/foo/baz.h"

class Bar {
public:
  virtual ~Bar() {}

  virtual void someThing() PURE;
  ...
```

This would require the addition to `include/envoy/foo/BUILD` of the following target:

```python
envoy_cc_library(
    name = "bar_interface",
    hdrs = ["bar.h"],
    deps = [
        ":baz_interface",
        "//include/envoy/buffer:buffer_interface",
    ],
)
```

This declares a new target `bar_interface`, where the convention is that pure
virtual interfaces have their targets suffixed with `_interface`. The header
`bar.h` is exported to other targets that depend on
`//incude/envoy/foo:bar_interface`. The interface target itself depends on
`baz_interface` (in the same directory, hence the relative Bazel label) and
`buffer_interface`.

In general, any header included via `#include` in a file belonging to the union
of the `hdrs` and `srcs` lists for a Bazel target X should appear directly in
the exported `hdrs` list for some target Y listed in the `deps` of X.

Continuing the above example, the implementation of `Bar` might take place in
`source/common/foo/bar_impl.h`, e.g.

```c++
#pragma once

#include "envoy/foo/bar.h"

class BarImpl : public Bar {
...
```

and `source/common/foo/bar_impl.cc`:

```c++
#include "common/foo/bar_impl.h"

#include "common/buffer/buffer_impl.h"
#include "common/foo/bar_internal.h"
#include "common/foo/baz_impl.h"
...
```

The corresponding target to be added to `source/common/foo/BUILD` would be:

```python
envoy_cc_library(
    name = "bar_lib",
    srcs = [
        "bar_impl.cc",
        "bar_internal.h",
    ],
    hdrs = ["bar_impl.h"],
    deps = [
        ":baz_lib",
        "//include/envoy/foo:bar_interface",
        "//source/common/buffer:buffer_lib",
    ],
)
```

By convention, Bazel targets for internal implementation libraries are suffixed
with `_lib`.

Similar to the above, a test mock target might be declared for `test/mocks/foo/mocks.h` in
`test/mocks/foo/BUILD` with:

```python
envoy_cc_mock(
    name = "foo_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/foo:bar_interface",
        ...
    ],
)
```

Typically, mocks are provided for all interfaces in a directory in a single
`mocks.{cc,h}` and corresponding `_mocks` Bazel target. There are some
exceptions, such as [test/mocks/upstream/BUILD](../test/mocks/upstream/BUILD),
where more granular mock targets are defined.

Unit tests for `BarImpl` would be written in `test/common/foo/bar_impl_test.cc`
and a target added to `test/common/foo/BUILD`:

```python
envoy_cc_test(
    name = "bar_impl_test",
    srcs = ["bar_impl_test.cc"],
    deps = [
        "//test/mocks/buffer:buffer_mocks",
        "//source/common/foo:bar_lib",
        ...
    ],
)
```

## Binary targets

New binary targets, for example tools that make use of some Envoy libraries, can be added
with the `envoy_cc_binary` rule, e.g. for a new `tools/hello/world.cc` that depends on
`bar_lib`, we might have in `tools/hello/BUILD`:

```python
envoy_cc_binary(
    name = "world",
    srcs = ["world.cc"],
    deps = [
        "//source/common/foo:bar_lib",
    ],
)
```

## Filter linking

Filters are registered via static initializers at early runtime by modules in
[`source/server/config`](../source/server/config). These require the `alwayslink
= 1` attribute to be set in the corresponding `envoy_cc_library` target to
ensure they are correctly linked. See
[`source/server/config/http/BUILD`](../source/server/config/http/BUILD) for
examples.

## Tests with environment dependencies

Some tests depends on read-only data files. In general, these can be specified by adding a
`data = ["some_file.csv", ...],` attribute to the `envoy_cc_test` target, e.g.

```python
envoy_cc_test(
    name = "bar_impl_test",
    srcs = ["bar_impl_test.cc"],
    data = ["some_file.csv"],
    deps = [
        "//test/mocks/buffer:buffer_mocks",
        "//source/common/foo:bar_lib",
        ...
    ],
)
```

A [glob
function](https://bazel.build/versions/master/docs/be/functions.html#glob) is
available for simple pattern matching. Within a test, the read-only data dependencies
can be accessed via the
[`TestEnvironment::runfilesPath()`](../test/test_common/environment.h) method.

A writable path is provided for test temporary files by
[`TestEnvironment::temporaryDirectory()`](../test/test_common/environment.h).

Integration tests might rely on JSON files that require paths for writable
temporary files and paths for file-based Unix Domain Sockets to be specified in
the JSON. Jinja-style `{{ test_tmpdir }}` and `{{ test_udsdir }}` macros can be used as
placeholders, with the substituted JSON files made available in
[`TestEnvironment::temporaryDirectory()`](../test/test_common/environment.h) by
the `envoy_cc_test_with_json` rule, e.g.

```python
envoy_cc_test_with_json(
    name = "bar_integration_test",
    srcs = ["bar_integration_test.cc"],
    jsons = ["//test/config/integration:server.json"],
    deps = [
        "//source/server:server_lib",
        ...
    ],
)
```

In general, the `setup_cmds` attribute can be used to declare a setup shell
script that executes in the [test
environment](https://bazel.build/versions/master/docs/test-encyclopedia.html#initial-conditions)
prior to the test, see [`bazel/envoy_build_system.bzl`](envoy_build_system.bzl)
for further details.
