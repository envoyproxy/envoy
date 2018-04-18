# Envoy fuzz testing

Envoy is fuzz tested via [OSS-Fuzz](https://github.com/google/oss-fuzz). We
follow the best practices described in the OSS-Fuzz [ideal integration
page](https://github.com/google/oss-fuzz/blob/master/docs/ideal_integration.md).

## Test environment

Tests should be unit test-like, fast and not require writable access to the filesystem (beyond
temporary files), network (including loopback) or multiple processes. See the
[ClusterFuzz
environment](https://github.com/google/oss-fuzz/blob/master/docs/fuzzer_environment.md)
for further details.

## Corpus

Every fuzz test must comes with a *corpus*. A corpus is a set of files that
provide example valid inputs. Fuzzing libraries will use this seed corpus to
drive mutations, e.g. via evolutionary fuzzing, to explore interesting parts of
the state space.

The corpus also acts as a quick regression test for evaluating the fuzz tests
without the help of a fuzzing library.

The corpus is located in a directory underneath the fuzz test. E.g. suppose you
have
[`test/common/common/base64_fuzz_test.cc`](../../test/common/common/base64_fuzz_test.cc),
a corpus directory
[`test/common/common/base64_corpus`](../../test/common/common/base64_corpus) should
exist, populated with files that will act as the corpus.

## Test driver

Your fuzz test will ultimately be driven by a simple interface:

```c++
DEFINE_FUZZER(const uint8_t* data, size_t size) {
  // Your test code goes here
}
```

It is up to your test `DEFINE_FUZZER` implementation to map this buffer of data to
meaningful semantics, e.g. a stream of network bytes or a protobuf binary input.

The fuzz test will be executed in two environments:

1. Under Envoy's fuzz test driver when run in the Envoy repository with
   `bazel test //test/path/to/some_fuzz_test`. This provides a litmus test
   indicating that the test passes CI and basic sanitizers on the supplied
   corpus.

2. Via fuzzing library test drivers in OSS-Fuzz. This is where the real fuzzing
   takes places on a VM cluster and the seed corpus is used by fuzzers to
   explore the state space.

## Defining a new fuzz test

1. Write a fuzz test module implementing the `DEFINE_FUZZER`
   interface. E.g.
   [`test/common/common/base64_fuzz_test.cc`](../../test/common/common/base64_fuzz_test.cc).

2. Define an `envoy_cc_fuzz_test` target, see `base64_fuzz_test` in
   [`test/common/common/BUILD`](../../test/common/common/BUILD).

3. Create the seed corpus directory and populate it with at least one example
   input. E.g.
   [`test/common/common/base64_corpus`](../../test/common/common/base64_corpus).

4. Run the `envoy_cc_fuzz_test` target. E.g. `bazel test
   //test/common/common:base64_fuzz_test`.

## Protobuf fuzz tests

We also have integration with
[libprotobuf-mutator](https://github.com/google/libprotobuf-mutator), allowing
tests built on a protobuf input to work directly with a typed protobuf object,
rather than a raw buffer. The interface to this is as described at
https://github.com/google/libprotobuf-mutator#integrating-with-libfuzzer:

```c++
DEFINE_PROTO_FUZZER(const MyMessageType& input) {
  // Your test code goes here
}
```
