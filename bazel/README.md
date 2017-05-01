# Building Envoy with Bazel

## Production environments

To build Envoy with Bazel in a production environment, where the [Envoy
dependencies](https://lyft.github.io/envoy/docs/install/requirements.html) are typically
independently sourced, the following steps should be followed:

1. [Install Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. Configure, build and/or install the [Envoy dependencies](https://lyft.github.io/envoy/docs/install/requirements.html).
3. Configure a Bazel [WORKSPACE](https://bazel.build/versions/master/docs/be/workspace.html)
   to point Bazel at the Envoy dependencies. An example is provided in the CI Docker image
   [WORKSPACE](https://github.com/lyft/envoy/blob/master/ci/WORKSPACE) and corresponding
   [BUILD](https://github.com/lyft/envoy/blob/master/ci/prebuilt/BUILD) files.
4. `bazel build --package_path %workspace%:<path to Envoy source tree> //source/exe:envoy-static`
   from the directory containing your WORKSPACE.

## Quick start Bazel build for developers

As a developer convenience, a [WORKSPACE](https://github.com/lyft/envoy/blob/master/WORKSPACE) and
[rules for building a recent
version](https://github.com/lyft/envoy/blob/master/bazel/repositories.bzl) of the various Envoy
dependencies are provided. These are provided as is, they are only suitable for development and
testing purposes. The specific versions of the Envoy dependencies used in this build may not be
up-to-date with the latest security patches.

1. [Install Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. `bazel fetch //source/...` to fetch and build all external dependencies. This may take some time.
2. `bazel build //source/exe:envoy-static` from the Envoy source directory.

## Building Bazel with the CI Docker image

Bazel can also be built with the Docker image used for CI, by installing Docker and executing:

```
./ci/run_envoy_docker.sh ./ci/do_ci.sh bazel.fastbuild
```

See also the [documentation](https://github.com/lyft/envoy/tree/master/ci) for developer use of the
CI Docker image.

## Using a compiler toolchain in a non-standard location

By setting the `CC` and `LD_LIBRARY_PATH` in the environment that Bazel executes from as
appropriate, an arbitrary compiler toolchain and standard library location can be specified. One
slight caveat is that (at the time of writing), Bazel expects the binutils in `$(dirname $CC)` to be
unprefixed, e.g. `as` instead of `x86_64-linux-gnu-as`.

# Testing Envoy with Bazel

All the Envoy tests can be built and run with:

```
bazel test //test/...
```

An individual test target can be run with a more specific Bazel
[label](https://bazel.build/versions/master/docs/build-ref.html#Labels), e.g. to build and run only
the units tests in
[test/common/http/async_client_impl_test.cc](https://github.com/lyft/envoy/blob/master/test/common/http/async_client_impl_test.cc):

```
bazel test //test/common/http:async_client_impl_test
```

To observe more verbose test output:

```
bazel test --test_output=streamed //test/common/http:async_client_impl_test
```

It's also possible to pass into an Envoy test additional command-line args via `--test_arg`. For
example, for extremely verbose test debugging:

```
bazel test --test_output=streamed //test/common/http:async_client_impl_test --test_arg="-l trace"
```

Bazel will by default cache successful test results. To force it to rerun tests:


```
bazel test //test/common/http:async_client_impl_test --cache_test_results=no
```

Bazel will by default run all tests inside a sandbox, which disallows access to the
local filesystem. If you need to break out of the sandbox (for example to run under a
local script or tool with [`--run_under`](https://bazel.build/versions/master/docs/bazel-user-manual.html#flag--run_under)),
you can run the test with `--strategy=TestRunner=standalone`, e.g.:

```
bazel test //test/common/http:async_client_impl_test --strategy=TestRunner=standalone --run_under=/some/path/foobar.sh
```
# Stack trace symbol resolution

Envoy can produce backtraces on demand and from assertions and other fatal
actions like segfaults.  The stack traces written in the log or to stderr contain
addresses rather than resolved symbols.  The `tools/stack_decode.py` script exists
to process the output and do symbol resolution to make the stack traces useful.  Any
log lines not relevant to the backtrace capability are passed through the script unchanged
(it acts like a filter).

The script runs in one of two modes. If passed no arguments it anticipates
Envoy (or test) output on stdin. You can postprocess a log or pipe the output of
an Envoy process. If passed some arguments it runs the arguments as a child
process. This enables you to run a test with backtrace post processing. Bazel
sandboxing must be disabled by specifying standalone execution. Example
command line:

```
bazel test -c dbg //test/server:backtrace_test
--run_under=`pwd`/tools/stack_decode.py --strategy=TestRunner=standalone
--cache_test_results=no --test_output=all
```

You will need to use either a `dbg` build type or the `opt` build type to get symbol
information in the binaries.

By default main.cc will install signal handlers to print backtraces at the
location where a fatal signal occurred.  The signal handler will re-raise the
fatal signal with the default handler so a core file will still be dumped after
the stack trace is logged.  To inhibit this behavior use
`--define=signal_trace=disabled` on the Bazel command line. No signal handlers will
be installed.

# Running a single Bazel test under GDB

```
tools/bazel-test-gdb //test/common/http:async_client_impl_test -c dbg
```

Without the `-c dbg` Bazel option at the end of the command line the test
binaries will not include debugging symbols and GDB will not be very useful.

# Additional Envoy build and test options

In general, there are 3 [compilation
modes](https://bazel.build/versions/master/docs/bazel-user-manual.html#flag--compilation_mode)
that Bazel supports:

* `fastbuild`: `-O0`, aimed at developer speed (default).
* `opt`: `-O2 -DNDEBUG -ggdb3`, for production builds and performance benchmarking.
* `dbg`: `-O0 -ggdb3`, no optimization and debug symbols.

You can use the `-c <compilation_mode>` flag to control this, e.g.

```
bazel build -c opt //source/exe:envoy-static
```
To build and run tests with the compiler's address sanitizer (ASAN) enabled:

```
bazel test -c dbg --config=asan //test/...
```

The ASAN failure stack traces include line numbers as a results of running ASAN with a `dbg` build above.

# Release builds

Release builds should be built in `opt` mode, processed with `strip` and have a
`.note.gnu.build-id` section with the Git SHA1 at which the build took place.
They should also ignore any local `.bazelrc` for reproducibility. This can be
achieved with:

```
bazel --bazelrc=/dev/null build -c opt //source/exe:envoy-static.stripped.stamped
```

One caveat to note is that the Git SHA1 is truncated to 16 bytes today as a
result of the workaround in place for
https://github.com/bazelbuild/bazel/issues/2805.

# Coverage builds

To generate coverage results, make sure you have `gcov` 3.3 in your `PATH` (or
set `GCOVR` to point at it). Then run:

```
test/run_envoy_bazel_coverage.sh
```

The summary results are printed to the standard output and the full coverage
report is available in `generated/coverage/coverage.html`.

# Cleaning the build and test artifacts

`bazel clean` will nuke all the build/test artifacts from the Bazel cache for
Envoy proper. To remove the artifacts for the external dependencies run
`bazel clean --expunge`.

If something goes really wrong and none of the above work to resolve a stale build issue, you can
always remove your Bazel cache completely. It is likely located in `~/.cache/bazel`.

# Adding or maintaining Envoy build rules

See the [developer guide for writing Envoy Bazel rules](DEVELOPER.md).

# Bazel performance on (virtual) machines with low resources

If the (virtual) machine that is performing the build is low on memory or CPU
resources, you can override Bazel's default job parallelism determination with
`--jobs=N` to restrict the build to at most `N` simultaneous jobs, e.g.:

```
bazel build --jobs=2 //source/...
```

# Debugging the Bazel build

When trying to understand what Bazel is doing, the `-s` and `--explain` options
are useful. To have Bazel provide verbose output on which commands it is executing:

```
bazel build -s //source/...
```

To have Bazel emit to a text file the rationale for rebuilding a target:

```
bazel build --explain=file.txt //source/...
```

To get more verbose explanations:

```
bazel build --explain=file.txt --verbose_explanations //source/...
```

# Resolving paths in bazel build output

Sometimes it's useful to see real system paths in bazel error message output (vs. symbolic links).
`tools/path_fix.sh` is provided to help with this. See the comments in that file.
