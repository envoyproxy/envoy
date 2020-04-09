# CPU or memory consumption testing with `pprof`

To use `pprof` to analyze performance and memory consumption in Envoy, you can
use the built-in statically linked profiler, or dynamically link it in to a
specific place yourself.

## Collecting CPU or heap profile for a full execution of envoy

Static linking is already available (because of a `HeapProfilerDump()` call
inside
[`Envoy::Profiler::Heap::stopProfiler())`](https://github.com/envoyproxy/envoy/blob/master/source/common/profiler/profiler.cc#L32-L39)).

### Compiling a statically-linked Envoy

Build the static binary using bazel:

    $ bazel build //source/exe:envoy-static

### Collecting the profile

To collect a heap profile, run a statically-linked Envoy with `pprof`

and run the binary with a `CPUPROFILE` or `HEAPPROFILE` environment variable, like so:

    $ CPUPROFILE=/tmp/mybin.cpuprof bazel-bin/source/exe/envoy-static <args>
    $ HEAPPROFILE=/tmp/mybin.heapprof bazel-bin/source/exe/envoy-static <args>

`CPUPROFILE` or `HEAPPROFILE` sets a location for the profiler output. (See *Methodology*.)

There are several other environment variables that can be set to tweak the behavior of gperftools. See https://gperftools.github.io/gperftools/ for more details.

### Analyzing the profile

[pprof](https://github.com/google/pprof) can be used to symbolize CPU and heap profiles. For example:

    $ pprof -text bazel-bin/source/exe/envoy-static /tmp/mybin.cpuprof

## Collecting CPU or heap profile for the full execution of a test target

The profiler library is automatically linked into envoy_cc_test targets.

Run a test with heap profiling enabled, like so:

    $ bazel test --test_env=HEAPPROFILE=/tmp/heapprof <test target>

Run a test with CPU profiling enabled, like so:

    $ bazel test --test_env=CPUPROFILE=/tmp/cpuprof <test target>

Note that heap checks and heap profile collection in tests have noticiable performance implications. Use the following command to collect a CPU profile from a test target with heap check and heap profile collection disabled:

    $ bazel test --test_env=CPUPROFILE=/tmp/cpuprof --test_env=HEAPPROFILE= --test_env=HEAPCHECK= <test target>

## Starting and stopping profile programmatically

### Add `tcmalloc_dep` dependency to envoy_cc_library rules

It is possible to start/stop the CPU or heap profiler programmatically.
The [Gperftools CPU Profiler](https://gperftools.github.io/gperftools/cpuprofile.html)
is controlled by `ProfilerStart()`/`ProfilerStop()`, and the
[Gperftools Heap Profiler](https://gperftools.github.io/gperftools/heapprofile.html)
is controlled by `HeapProfilerStart()`, `HeapProfilerStop()` and `HeapProfilerDump()`.

These functions are wrapped by Envoy objects defined in [`source/common/profiler/profiler.h`](https://github.com/envoyproxy/envoy/blob/master/source/common/profiler/profiler.h)).

To enable profiling programmatically:

1. Add a library dependency on "//source/common/profiler:profiler_lib" to your envoy_cc_library build rule.
2. Use the `startProfiler`/`stopProfiler` methods of `Envoy::Profiler::Cpu` or `Envoy::Profiler::Heap` to collect a profile.

Note that `startProfiler` should only be called if no other profile of that type is currently active (e.i. `profilerEnabled()` returns false).

Example:

```c++
    // includes
    #include "common/profiler/profiler.h"
    ...
    Function(...) {
        if (!Profiler::Cpu::startProfiler(profile_path)) {
           // Error handling
        }
        ...
        Do expensive stuff in one or more threads.
        ...

        // Stop the profiler and dump output to the `profile_path` specified when profile was started.
        Profiler::Cpu::stopProfiler();
    }
```

## Memory Profiling in Tests
To support memory leaks detection, tests are built with gperftools dependencies enabled by default.

### Enabling Memory Profiling in Tests
Use `HeapProfilerStart()`, `HeapProfilerStop()`, and `HeapProfilerDump()` to start, stop, and persist
memory dumps, respectively. Please see [above](#adding-tcmalloc_dep-to-envoy) for more details.

### Bazel Configuration
By default, bazel executes tests in a sandbox, which will be deleted together with memory dumps
after the test run. To preserve memory dumps, bazel can be forced to run tests without
sandboxing, by setting the ```TestRunner``` parameter to ```local```:
```
bazel test --strategy=TestRunner=local ...
```

An alternative is to set ```HEAPPROFILE``` environment variable for the test runner:
```
bazel test --test_env=HEAPPROFILE=/tmp/testprofile ...
```

# Methodology

For consistent testing, it makes sense to run Envoy for a constant amount of
time across trials:

    $ timeout <num_seconds> bazel-bin/source/exe/envoy <options>

Envoy will print to stdout something like:

    Starting tracking the heap

And then a series of stdouts like:

    Dumping heap profile to <heap file 0001> (100 MB currently in use)
    Dumping heap profile to <heap file 0002> (200 MB currently in use)
    ...

This will generate a series of files; if you statically-linked, these are
wherever `HEAPPROFILE` points to. Otherwise, they are in the current directory
by default. They'll be named something like `main_common_base.0001.heap`,
`main_common_base.0002.heap`, etc.

*NB:* There is no reason this needs to be titled `main_common_base`. Whatever
flag you supply `HeapProfilerStart` / `HeapProfilerDump` will become the
filename. Multiple sections of code could be profiled simultaneously by setting
multiple `HeapProfilerStart()` / `HeapProfilerStop()` breakpoints with unique
identifiers.

# Analyzing with `pprof`

[pprof](https://github.com/google/pprof) can read these heap files in a
number of ways. Most convenient for first-order inspection might be `pprof -top`
or `pprof -text`:

    $ pprof -text bazel-bin/source/exe/envoy main_common_base* | head -n5
    File: envoy
    Build ID: ...
    Type: inuse_space
    Showing nodes accounting for 6402800.62kB, 98.59% of 6494044.58kB total
    Dropped ... nodes (cum <= ...kB)

More complex flame/graph charts can be generated and viewed in a browser, which
is often more helpful than text-based output:

    $ pprof -http=localhost:9999 bazel-bin/source/exe/envoy main_common_base*
