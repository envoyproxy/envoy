# Building Envoy with Bazel

## Production environments

To build Envoy with Bazel in a production environment, where the [Envoy
dependencies](https://www.envoyproxy.io/docs/envoy/latest/install/building.html#requirements) are typically
independently sourced, the following steps should be followed:

1. Install the latest version of [Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. Configure, build and/or install the [Envoy dependencies](https://www.envoyproxy.io/docs/envoy/latest/install/building.html#requirements).
3. Configure a Bazel [WORKSPACE](https://bazel.build/versions/master/docs/be/workspace.html)
   to point Bazel at the Envoy dependencies. An example is provided in the CI Docker image
   [WORKSPACE](https://github.com/envoyproxy/envoy/blob/master/ci/WORKSPACE) and corresponding
   [BUILD](https://github.com/envoyproxy/envoy/blob/master/ci/prebuilt/BUILD) files.
4. `bazel build --package_path %workspace%:<path to Envoy source tree> //source/exe:envoy-static`
   from the directory containing your WORKSPACE.

## Quick start Bazel build for developers

As a developer convenience, a [WORKSPACE](https://github.com/envoyproxy/envoy/blob/master/WORKSPACE) and
[rules for building a recent
version](https://github.com/envoyproxy/envoy/blob/master/bazel/repositories.bzl) of the various Envoy
dependencies are provided. These are provided as is, they are only suitable for development and
testing purposes. The specific versions of the Envoy dependencies used in this build may not be
up-to-date with the latest security patches. See 
[this doc](https://github.com/envoyproxy/envoy/blob/master/bazel/EXTERNAL_DEPS.md#updating-an-external-dependency-version)
for how to update or override dependencies.

1. Install the latest version of [Bazel](https://bazel.build/versions/master/docs/install.html) in your environment.
2. Install external dependencies libtool, cmake, and realpath libraries separately.
On Ubuntu, run the following commands:
```
 apt-get install libtool
 apt-get install cmake
 apt-get install realpath
 apt-get install clang-format-5.0
```

On Fedora (maybe also other red hat distros), run the following:
```
dnf install cmake libtool libstdc++
```

On OS X, you'll need to install several dependencies. This can be accomplished via Homebrew:
```
brew install coreutils # for realpath
brew install wget
brew install cmake
brew install libtool
brew install go
brew install bazel
brew install automake
```

Envoy compiles and passes tests with the version of clang installed by XCode 9.3.0:
Apple LLVM version 9.1.0 (clang-902.0.30).

3. Install Golang on your machine. This is required as part of building [BoringSSL](https://boringssl.googlesource.com/boringssl/+/HEAD/BUILDING.md)
and also for [Buildifer](https://github.com/bazelbuild/buildtools) which is used for formatting bazel BUILD files.
4. `go get github.com/bazelbuild/buildtools/buildifier` to install buildifier
5. `bazel fetch //source/...` to fetch and build all external dependencies. This may take some time.
6. `bazel build //source/exe:envoy-static` from the Envoy source directory.

## Building Bazel with the CI Docker image

Bazel can also be built with the Docker image used for CI, by installing Docker and executing:

```
./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.dev'
```

See also the [documentation](https://github.com/envoyproxy/envoy/tree/master/ci) for developer use of the
CI Docker image.

## Using a compiler toolchain in a non-standard location

By setting the `CC` and `LD_LIBRARY_PATH` in the environment that Bazel executes from as
appropriate, an arbitrary compiler toolchain and standard library location can be specified. One
slight caveat is that (at the time of writing), Bazel expects the binutils in `$(dirname $CC)` to be
unprefixed, e.g. `as` instead of `x86_64-linux-gnu-as`.

## Supported compiler versions

Though Envoy has been run in production compiled with GCC 4.9 extensively, we now require
GCC >= 5 due to known issues with std::string thread safety and C++14 support. Clang >= 4.0 is also
known to work.

## Clang STL debug symbols

By default Clang drops some debug symbols that are required for pretty printing to work correctly.
More information can be found [here](https://bugs.llvm.org/show_bug.cgi?id=24202). The easy solution
is to set ```--copt=-fno-limit-debug-info``` on the CLI or in your bazel.rc file.

# Testing Envoy with Bazel

All the Envoy tests can be built and run with:

```
bazel test //test/...
```

An individual test target can be run with a more specific Bazel
[label](https://bazel.build/versions/master/docs/build-ref.html#Labels), e.g. to build and run only
the units tests in
[test/common/http/async_client_impl_test.cc](https://github.com/envoyproxy/envoy/blob/master/test/common/http/async_client_impl_test.cc):

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

By default, testing exercises both IPv4 and IPv6 address connections. In IPv4 or IPv6 only
environments, set the environment variable ENVOY_IP_TEST_VERSIONS to "v4only" or
"v6only", respectively.

```
bazel test //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v4only
bazel test //test/... --test_env=ENVOY_IP_TEST_VERSIONS=v6only
```

By default, tests are run with the [gperftools](https://github.com/gperftools/gperftools) heap
checker enabled in "normal" mode to detect leaks. For other mode options, see the gperftools
heap checker [documentation](https://gperftools.github.io/gperftools/heap_checker.html). To
disable the heap checker or change the mode, set the HEAPCHECK environment variable:

```
# Disables the heap checker
bazel test //test/... --test_env=HEAPCHECK=
# Changes the heap checker to "minimal" mode
bazel test //test/... --test_env=HEAPCHECK=minimal
```

Bazel will by default cache successful test results. To force it to rerun tests:

```
bazel test //test/common/http:async_client_impl_test --cache_test_results=no
```

Bazel will by default run all tests inside a sandbox, which disallows access to the
local filesystem. If you need to break out of the sandbox (for example to run under a
local script or tool with [`--run_under`](https://docs.bazel.build/versions/master/user-manual.html#flag--run_under)),
you can run the test with `--strategy=TestRunner=standalone`, e.g.:

```
bazel test //test/common/http:async_client_impl_test --strategy=TestRunner=standalone --run_under=/some/path/foobar.sh
```
# Stack trace symbol resolution

Envoy can produce backtraces on demand and from assertions and other fatal
actions like segfaults. The stack traces written in the log or to stderr contain
addresses rather than resolved symbols. The `tools/stack_decode.py` script exists
to process the output and do symbol resolution to make the stack traces useful. Any
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
location where a fatal signal occurred. The signal handler will re-raise the
fatal signal with the default handler so a core file will still be dumped after
the stack trace is logged. To inhibit this behavior use
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
modes](https://docs.bazel.build/versions/master/user-manual.html#flag--compilation_mode)
that Bazel supports:

* `fastbuild`: `-O0`, aimed at developer speed (default).
* `opt`: `-O2 -DNDEBUG -ggdb3`, for production builds and performance benchmarking.
* `dbg`: `-O0 -ggdb3`, no optimization and debug symbols.

You can use the `-c <compilation_mode>` flag to control this, e.g.

```
bazel build -c opt //source/exe:envoy-static
```

## Sanitizers

To build and run tests with the gcc compiler's [address sanitizer
(ASAN)](https://github.com/google/sanitizers/wiki/AddressSanitizer) and
[undefined behavior
(UBSAN)](https://developers.redhat.com/blog/2014/10/16/gcc-undefined-behavior-sanitizer-ubsan) sanitizer enabled:

```
bazel test -c dbg --config=asan //test/...
```

The ASAN failure stack traces include line numbers as a result of running ASAN with a `dbg` build above.

If you have clang-5.0, additional checks are provided with:

```
bazel test -c dbg --config=clang-asan //test/...
```

Similarly, for [thread sanitizer (TSAN)](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual) testing:

```
bazel test -c dbg --config=clang-tsan //test/...
```

## Log Verbosity

By default, log verbosity is controlled at runtime in all builds. However, it may be desirable to
remove log statements of lower importance during compilation to enhance performance. To remove
`trace` and `debug` log statements during compilation define `NVLOG`:
```
bazel build --copt=-DNVLOG //source/exe:envoy-static
```

## Disabling optional features

The following optional features can be disabled on the Bazel build command-line:

* Hot restart with `--define hot_restart=disabled`
* Google C++ gRPC client with `--define google_grpc=disabled`
* Backtracing on signals with `--define signal_trace=disabled`

## Enabling optional features

The following optional features can be enabled on the Bazel build command-line:

* Exported symbols during linking with `--define exported_symbols=enabled`.
  This is useful in cases where you have a lua script that loads shared object libraries, such as those installed via luarocks.
* Perf annotation with `define perf_annotation=enabled` (see source/common/common/perf_annotation.h for details).

## Stats Tunables

The default maximum number of stats in shared memory, and the default
maximum length of a cluster/route config/listener name, can be
overridden at compile-time by defining `ENVOY_DEFAULT_MAX_STATS` and
`ENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH`, respectively, to the desired
value. For example:

```
bazel build --copts=-DENVOY_DEFAULT_MAX_STATS=32768 --copts=-DENVOY_DEFAULT_MAX_OBJ_NAME_LENGTH=150 //source/exe:envoy-static
```


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

To generate coverage results, make sure you have
[`gcovr`](https://github.com/gcovr/gcovr) 3.3 in your `PATH` (or set `GCOVR` to
point at it) and are using a GCC toolchain (clang does not work currently, see
https://github.com/envoyproxy/envoy/issues/1000). Then run:

```
test/run_envoy_bazel_coverage.sh
```

The summary results are printed to the standard output and the full coverage
report is available in `generated/coverage/coverage.html`.

Coverage for every PR is available in Circle in the "artifacts" tab of the coverage job. You will
need to navigate down and open "coverage.html" but then you can navigate per normal. NOTE: We
have seen some issues with seeing the artifacts tab. If you can't see it, log out of Circle, and
then log back in and it should start working.

The latest coverage report for master is available
[here](https://s3.amazonaws.com/lyft-envoy/coverage/report-master/coverage.html).

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
