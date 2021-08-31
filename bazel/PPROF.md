# Table of Contents

   * [CPU or memory consumption testing with gperftools and pprof](#cpu-or-memory-consumption-testing-with-gperftools-and-pprof)
      * [Collecting CPU or heap profile for a full execution of envoy](#collecting-cpu-or-heap-profile-for-a-full-execution-of-envoy)
         * [Compiling a statically-linked Envoy](#compiling-a-statically-linked-envoy)
         * [Collecting the profile](#collecting-the-profile)
         * [Analyzing the profile](#analyzing-the-profile)
      * [Collecting CPU or heap profile for the full execution of a test target](#collecting-cpu-or-heap-profile-for-the-full-execution-of-a-test-target)
      * [Starting and stopping profile programmatically](#starting-and-stopping-profile-programmatically)
         * [Add tcmalloc_dep dependency to envoy_cc_library rules](#add-tcmalloc_dep-dependency-to-envoy_cc_library-rules)
      * [Memory Profiling in Tests](#memory-profiling-in-tests)
         * [Enabling Memory Profiling in Tests](#enabling-memory-profiling-in-tests)
         * [Bazel Configuration](#bazel-configuration)
   * [Methodology](#methodology)
   * [Analyzing with pprof](#analyzing-with-pprof)
   * [Alternatives to gperftools](#alternatives-to-gperftools)
      * [On-CPU analysis](#on-cpu-analysis)
      * [Memory analysis](#memory-analysis)
   * [Performance annotations](#performance-annotations)

# CPU or memory consumption testing with `gperftools` and `pprof`

To use `pprof` to analyze performance and memory consumption in Envoy, you can
use the built-in statically linked profiler provided by
[gperftools](https://github.com/gperftools/gperftools), or dynamically link it in to a
specific place yourself.

## Collecting CPU or heap profile for a full execution of envoy

Static linking is already available (because of a `HeapProfilerDump()` call
inside
[`Envoy::Profiler::Heap::stopProfiler())`](https://github.com/envoyproxy/envoy/blob/main/source/common/profiler/profiler.cc#L32-L39)).

### Compiling a statically-linked Envoy

Build the static binary using bazel:

    $ bazel build --define tcmalloc=gperftools //source/exe:envoy-static

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

    $ bazel test --test_env=HEAPPROFILE=/tmp/heapprof --define tcmalloc=gperftools <test target>

Run a test with CPU profiling enabled, like so:

    $ bazel test --test_env=CPUPROFILE=/tmp/cpuprof --define tcmalloc=gperftools <test target>

Note that heap checks and heap profile collection in tests have noticiable performance implications. Use the following command to collect a CPU profile from a test target with heap check and heap profile collection disabled:

    $ bazel test --test_env=CPUPROFILE=/tmp/cpuprof --test_env=HEAPPROFILE= --test_env=HEAPCHECK= --define tcmalloc=gperftools <test target>

## Starting and stopping profile programmatically

### Add `tcmalloc_dep` dependency to envoy_cc_library rules

It is possible to start/stop the CPU or heap profiler programmatically.
The [Gperftools CPU Profiler](https://gperftools.github.io/gperftools/cpuprofile.html)
is controlled by `ProfilerStart()`/`ProfilerStop()`, and the
[Gperftools Heap Profiler](https://gperftools.github.io/gperftools/heapprofile.html)
is controlled by `HeapProfilerStart()`, `HeapProfilerStop()` and `HeapProfilerDump()`.

These functions are wrapped by Envoy objects defined in [`source/common/profiler/profiler.h`](https://github.com/envoyproxy/envoy/blob/main/source/common/profiler/profiler.h)).

To enable profiling programmatically:

1. Add a library dependency on "//source/common/profiler:profiler_lib" to your envoy_cc_library build rule.
2. Use the `startProfiler`/`stopProfiler` methods of `Envoy::Profiler::Cpu` or `Envoy::Profiler::Heap` to collect a profile.

Note that `startProfiler` should only be called if no other profile of that type is currently active (e.i. `profilerEnabled()` returns false).

Example:

```c++
    // includes
    #include "source/common/profiler/profiler.h"
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

# Alternatives to `gperftools`

## On-CPU analysis

By default Envoy is built without gperftools. In this case the same results can be
achieved for On-CPU analysis with the `perf` tool. For this there is no need to tweak
Envoy's environment, you can even do measurements for an instance running in production
(beware of possible performance hit though). Simply run:
```
$ perf record -g -F 99 -p `pgrep envoy`
^C[ perf record: Woken up 1 times to write data ]
[ perf record: Captured and wrote 0.694 MB perf.data (1532 samples) ]
```

The program will store the collected sampling data in the file `perf.data`. After installing
[perf_to_profile](https://github.com/google/perf_data_converter) this format
is also understood by recent enough versions of `pprof`:
```
$ pprof -http=localhost:9999 /path/to/envoy perf.data
```

Note that to see correct function names you need to pass an Envoy binary with debug symbols retained.
Its version must be the same as the version of the profiled Envoy binary.
You can get it from [envoyproxy/envoy-debug](https://hub.docker.com/r/envoyproxy/envoy-debug).

Alternatively, you can use [allegro/envoy-perf-pprof](https://github.com/allegro/envoy-perf-pprof) which
wraps the pprof setup mentioned above (installing perf_to_profile, pprof and getting
the proper Envoy debug version) in a Dockerfile.

## Memory analysis

Unfortunately `perf` doesn't support heap profiling analogous to `gperftools`, but still
we can get some insight into memory allocations with
[Brendan Gregg's tools](http://www.brendangregg.com/FlameGraphs/memoryflamegraphs.html).
You'll need to have [bcc](https://github.com/iovisor/bcc) installed in your system and a
copy of [FlameGraph](https://github.com/brendangregg/FlameGraph):
```
$ git clone https://github.com/brendangregg/FlameGraph
$ sudo /usr/share/bcc/tools/stackcount -p `pgrep envoy` \
    -U "/full/path/to/envoy/bazel-bin/source/exe/envoy-static:_Znwm" > out.stack
$ ./FlameGraph/stackcollapse.pl < out.stacks | ./FlameGraph/flamegraph.pl --color=mem \
    --title="operator new(std::size_t) Flame Graph" --countname="calls" > out.svg
```

The `stackcount` utility counts function calls and their stack traces using eBPF probes.
Since Envoy by default links statically to tcmalloc which provides its own implementation
of memory management functions the used uprobe looks like
```/full/path/to/envoy/bazel-bin/source/exe/envoy-static:_Znwm```. The part before
the colon is a library name (a path to Envoy's binary in our case). The part after the
colon is a function name as it looks like in the output of `objdump -tT /path/to/lib`,
that is mangled in our case. To get an idea how your compiler mangles the name you
can use this one-liner:
```
$ echo -e "#include <new>\n void* operator new(std::size_t) {} " | g++ -x c++ -S - -o- 2> /dev/null
        .file   ""
        .text
        .globl  _Znwm
        .type   _Znwm, @function
_Znwm:
.LFB73:
        .cfi_startproc
        pushq   %rbp
        .cfi_def_cfa_offset 16
        .cfi_offset 6, -16
        movq    %rsp, %rbp
        .cfi_def_cfa_register 6
        movq    %rdi, -8(%rbp)
        nop
        popq    %rbp
        .cfi_def_cfa 7, 8
        ret
        .cfi_endproc
.LFE73:
        .size   _Znwm, .-_Znwm
        .ident  "GCC: (GNU) 10.2.1 20201016 (Red Hat 10.2.1-6)"
        .section        .note.GNU-stack,"",@progbits
```

WARNING: The name is going to be different on 32-bit and 64-bit platforms due to different sizes
of `size_t`. Also ```void* operator new[](std::size_t)``` is a separate function as well as `malloc()`.
The latter is a C function and hence not mangled by the way.

`stackcount` doesn't count how much memory is allocated, but how often. To answer the "how much"
question you could use Brendan's
[mallocstacks](https://github.com/brendangregg/BPF-tools/blob/master/old/2017-12-23/mallocstacks.py)
tool, but it works only for `malloc()` calls. You need to modify it to take into
account other memory allocating functions.

# Performance annotations

In case there is a need to measure how long a code path takes time to execute in Envoy you may
resort to instrumenting the code with the
[performance annotations](https://github.com/envoyproxy/envoy/blob/main/source/common/common/perf_annotation.h).

There are two types of the annotations. The first one is used to measure operations limited by
a common lexical scope. For example:

```c++
void doHeavyLifting() {
  PERF_OPERATION(op);
  bool success = doSomething();
  if (success) {
    finalizeSuccessfulOperation();
    PERF_RECORD(op, "successful", "heavy lifting");
  } else {
    recoverFromFailure();
    PERF_RECORD(op, "failed", "heavy lifting")
  }
}
```

The recorded performance data can be dumped to stdout with a call to `PERF_DUMP()`:
```
Duration(us)  # Calls  Mean(ns)  StdDev(ns)  Min(ns)  Max(ns)  Category    Description
        2837       22    128965     37035.5   109731   241957  successful  heavy lifting
         204       13     15745      2449.4    13323    20446  failed      heavy lifting
```

The second type is performance annotations owned by a class instance. They can measure
operations spanned across the instance's methods:
```c++
class CustomFilter : public Http::StreamEncoderFilter {
public:

  ...
  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    PERF_OWNED_OPERATION(perf_operation_);
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override {
    if (end_stream) {
      PERF_OWNED_RECORD(perf_operation_, "without trailers", "stream encoding")
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap&) override {
    PERF_OWNED_RECORD(perf_operation_, "with trailers", "stream encoding");
    return Http::FilterTrailersStatus::Continue;
  }

  ...

private:
  ...
  PERF_OWNER(perf_operation_);
};
```
