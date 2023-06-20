# Envoy fuzz testing

Envoy is fuzz tested via [OSS-Fuzz](https://github.com/google/oss-fuzz). We follow the best
practices described in the OSS-Fuzz [ideal integration
page](https://github.com/google/oss-fuzz/blob/master/docs/ideal_integration.md).

## Test environment

Tests should be unit test-like, fast and not require writable access to the filesystem (beyond
temporary files), network (including loopback) or multiple processes. See the [ClusterFuzz
environment](https://github.com/google/oss-fuzz/blob/master/docs/fuzzer_environment.md) for further
details.

## Corpus

Every fuzz test must comes with a *corpus*. A corpus is a set of files that provide example valid
inputs. Fuzzing libraries will use this seed corpus to drive mutations, e.g. via evolutionary
fuzzing, to explore interesting parts of the state space.

The corpus also acts as a quick regression test for evaluating the fuzz tests without the help of a
fuzzing library.

The corpus is located in a directory underneath the fuzz test. E.g. suppose you have
[`test/common/common/base64_fuzz_test.cc`](../../test/common/common/base64_fuzz_test.cc), a corpus
directory [`test/common/common/base64_corpus`](../../test/common/common/base64_corpus) should exist,
populated with files that will act as the corpus.

## Test driver

Your fuzz test will ultimately be driven by a simple interface:

```c++
DEFINE_FUZZER(const uint8_t* data, size_t size) {
  // Your test code goes here
}
```

It is up to your test `DEFINE_FUZZER` implementation to map this buffer of data to meaningful
semantics, e.g. a stream of network bytes or a protobuf binary input.

The fuzz test will be executed in three environments:

1. Under Envoy's fuzz test driver when run in the Envoy repository with `bazel test
   //test/path/to/some_fuzz_test`. This provides a litmus test indicating that the test passes CI
   and basic sanitizers just on the supplied corpus.

1. Using the libFuzzer fuzzing engine and ASAN when run in the Envoy repository with `bazel run
   //test/path/to/some_fuzz_test --config asan-fuzzer`. This is where real fuzzing
   takes place locally. The built binary can take libFuzzer command-line flags, including the number
   of runs and the maximum input length.

3. Via fuzzing library test drivers in OSS-Fuzz. This is where the real fuzzing takes places on a VM
   cluster and the seed corpus is used by fuzzers to explore the state space.

## Defining a new fuzz test

1. Write a fuzz test module implementing the `DEFINE_FUZZER` interface. E.g.
   [`test/common/common/base64_fuzz_test.cc`](../../test/common/common/base64_fuzz_test.cc).

2. Define an `envoy_cc_fuzz_test` target, see `base64_fuzz_test` in
   [`test/common/common/BUILD`](../../test/common/common/BUILD).

3. Create the seed corpus directory and populate it with at least one example input. E.g.
   [`test/common/common/base64_corpus`](../../test/common/common/base64_corpus).

4. Run the `envoy_cc_fuzz_test` target to test against the seed corpus. E.g. `bazel test
   //test/common/common:base64_fuzz_test`.

5. Run the `*_fuzz_test` target against libFuzzer. E.g. `bazel run
   //test/common/common:base64_fuzz_test --config asan-fuzzer`.

## Protobuf fuzz tests

We also have integration with [libprotobuf-mutator](https://github.com/google/libprotobuf-mutator),
allowing tests built on a protobuf input to work directly with a typed protobuf object, rather than
a raw buffer. The interface to this is as described at
https://github.com/google/libprotobuf-mutator#integrating-with-libfuzzer:

```c++
DEFINE_PROTO_FUZZER(const MyMessageType& input) {
  // Your test code goes here
}
```

## Running fuzz tests locally

Within the Envoy repository, we have various `*_fuzz_test` targets. When run under `bazel test`,
these will exercise the corpus as inputs but not actually link and run against any fuzzer (e.g.
[`libfuzzer`](https://llvm.org/docs/LibFuzzer.html)).

To get actual fuzzing performed, the `*_fuzz_test` target needs to be built with `--config
asan-fuzzer`. This links the target to the libFuzzer fuzzing engine. This is recommended when
writing new fuzz tests to check if they pick up any low hanging fruit (i.e. what you can find on
your local machine vs. the fuzz cluster). The binary takes the location of the seed corpus
directory. Fuzzing continues indefinitely until a bug is found or the number of iterations it should
perform is specified with `-runs`. For example,

```console
bazel run //test/common/common:base64_fuzz_test --config asan-fuzzer \
    -- test/common/common/base64_corpus -runs=1000
```

The fuzzer prints information to stderr:

```
INFO: Seed: 774517650
INFO: Loaded 1 modules   (1090433 guards): 1090433 [0x8875600, 0x8c9e404),
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 4096 bytes
INFO: A corpus is not provided, starting from an empty corpus
#2	INITED cov: 47488 ft: 30 corp: 1/1b lim: 4 exec/s: 0 rss: 139Mb
#5	NEW    cov: 47499 ft: 47 corp: 2/3b lim: 4 exec/s: 0 rss: 139Mb L: 2/2 MS: 3 ChangeByte-ShuffleBytes-InsertByte-
...
#13145	NEW    cov: 47506 ft: 205 corp: 17/501b lim: 128 exec/s: 6572 rss: 150Mb L: 128/128 MS: 1 CrossOver-
#16384	pulse  cov: 47506 ft: 205 corp: 17/501b lim: 156 exec/s: 8192 rss: 151Mb
#32768	pulse  cov: 47506 ft: 205 corp: 17/501b lim: 317 exec/s: 6553 rss: 157Mb
#39442	NEW    cov: 47506 ft: 224 corp: 18/890b lim: 389 exec/s: 6573 rss: 160Mb L: 389/389 MS: 2 InsertByte-CrossOver-
```

Each output line reports statistics such as:
* `cov:` -- the number of code blocks or edges in the program's control flow graph observed so
  far. The number grows as the fuzzer finds interesting inputs.
* `exec/s:` -- the number of fuzzer iterations per second.
* `corp:` -- size of the corpus in memory (number of elements, size in bytes)
* `rss:` -- memory consumption.


## Running fuzz tests in OSS-Fuzz

Fuzzing against other engines is also performed by the
[oss-fuzz](https://github.com/google/oss-fuzz) project, with results provided on the [ClusterFuzz
dashboard](https://oss-fuzz.com).

It is possible to run against fuzzers locally by using the `oss-fuzz` Docker image. This will
provide fuzzing against other fuzzing engines.

1. `git clone https://github.com/google/oss-fuzz.git`
2. `cd oss-fuzz`
3. `python infra/helper.py build_image envoy`
4. `python infra/helper.py build_fuzzers --sanitizer=address envoy <path to envoy source tree>`. The
   path to the Envoy source tree can be omitted if you want to consume Envoy from GitHub at
   HEAD/main.
5. `python infra/helper.py run_fuzzer envoy <fuzz test target>`. The fuzz test target will be the
   test name, e.g. `server_fuzz_test`.

If there is a crash, `run_fuzzer` will emit a log line along the lines of:

```
artifact_prefix='./'; Test unit written to ./crash-db2ee19f50162f2079dc0c5ba24fd0e3dcb8b9bc
```

The test input can be found in `build/out/envoy`, e.g.
`build/out/envoy/crash-db2ee19f50162f2079dc0c5ba24fd0e3dcb8b9bc`. For protobuf fuzz tests, this will
be in text proto format.

To test and validate fixes to the crash, add it to the corpus in the Envoy source directory for the
test, e.g. for `server_fuzz_test` this is `test/server/corpus`, and run `bazel test`, e.g. `bazel
test //test/server:server_fuzz_test`. These crash cases can be added to the corpus in followup PRs
to provide fuzzers some interesting starting points for invalid inputs.

## Coverage reports

Coverage reports, where individual lines are annotated with fuzzing hit counts, are a useful way to
understand the scope and efficacy of the Envoy fuzzers. You can generate fuzz coverage reports both locally, and using the OSS-Fuzz infrastructure.

To generate fuzz coverage reports locally (see [Coverage builds](bazel/README.md), run
```
FUZZ_COVERAGE=true test/run_envoy_bazel_coverage.sh
```
This generates a coverage report after running the fuzz targets for one minute against the fuzzing engine libfuzzer and using the checked-in corpus as an initial seed.

Otherwise, you can generate reports from the
ClusterFuzz corpus following the general ClusterFuzz [instructions for profiling
setup](https://github.com/google/oss-fuzz/blob/master/docs/code_coverage.md).

To filter out unrelated artifacts (e.g. Bazel cache, libfuzzer src), the following profile command
can be used:

```bash
python infra/helper.py profile envoy -- \
  -ignore-filename-regex='proc/self/cwd/bazel-out.*' \
  -ignore-filename-regex='proc/self/cwd/external.*' \
  -ignore-filename-regex='proc/self/cwd/test.*' \
  -ignore-filename-regex='.*\.cache.*' \
  -ignore-filename-regex='src/libfuzzer.*'
```
