# Memory consumption testing with `pprof`

To use `pprof` to analyze performance and memory consumption in Envoy, you can
use the built-in statically linked profiler, or dynamically link it in to a
specific place yourself.

# Linking

## Static Linking

Static linking is already available (because of a `HeapProfilerDump()` call
inside
[`Envoy::Profiler::Heap::forceLink()`](https://github.com/envoyproxy/envoy/blob/master/source/common/profiler/profiler.cc#L21-L26)).

### Compiling a statically-linked Envoy

Build the static binary using bazel:

    $ bazel build //source/exe:envoy-static

### Running a statically-linked Envoy with `pprof`

And run the binary with a `HEAPPROFILE` environment variable, like so:

    $ HEAPPROFILE=/tmp/mybin.hprof bazel-bin/source/exe/envoy-static <args>

`HEAPPROFILE` sets a location for the profiler output. A statically-linked
binary must be run with this environment variable; a dynamically-linked binary
will populate the working directory by default. (See *Methodology*.)

## Dynamic Linking

### Adding `tcmalloc_dep` to Envoy

A statically-linked Envoy will profile everything. In a dynamically-linked
Envoy, you must add the HeapProfiler instructions yourself.
`HeapProfilerStart()` will start recording allocations, `HeapProfilerStop()`
will stop recording, and `HeapProfilerDump()` will dump an output to the
specified directory. (See [Gperftools Heap
Profiler](https://gperftools.github.io/gperftools/heapprofile.html).)

To add a `HeapProfiler` breakpoint yourself, add `tcmalloc` as a
dependency under the `envoy_cc_library` rule:

`source/exe/BUILD`

```c++
    envoy_cc_library(
       name = "envoy_common_lib",
+      tcmalloc_dep = 1,
       deps = [
       ...
    )
```

It is then necessary to add `HeapProfilerStart()` and `HeapProfilerDump()`
breakpoints somewhere in Envoy. One place to start profiling is at the
instantiation of `MainCommonBase::MainCommonBase`:

`source/exe/main_common.cc`

```c++
    // includes
    #include "gperftools/heap-profiler.h"
    ...
    MainCommonBase::MainCommonBase(...) : ... {
+       HeapProfilerStart("main_common_base"); // first line
        ...
    }
```

`source/server/server.cc`

```c++
    // includes
    #include "gperftools/heap-profiler.h"
    ...
    void InstanceImpl::Initialize(...) : ... {
        ...
+       HeapProfilerDump("main_common_base"); // last line
    }
```

Once these changes have been made in your working directory, it might make sense to
save the diff as a patch (`git diff > file`), which can then be quickly
applied/unapplied for testing and commiting. (`git apply`, `git apply -R`)

Build the binary using bazel, and run the binary without any environment variables:

    $ bazel build //source/exe:envoy
    $ bazel-bin/source/exe/envoy <args>

This will dump your profiler output to the working directory.

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
