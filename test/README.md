# Envoy tests

Envoy uses the [Google Test framework](https://github.com/google/googletest) for
tests, including
[Google Mock](https://github.com/google/googletest/blob/master/googlemock/README.md) for
mocks and matchers. See the documentation on those pages for information on the
various classes, macros, and matchers that Envoy uses from those frameworks.

## Integration tests

Envoy contains an integration testing framework, for testing
downstream-Envoy-upstream communication.
[See the framework's README for more information.](https://github.com/envoyproxy/envoy/blob/main/test/integration/README.md)

## Custom matchers

Envoy includes some custom Google Mock matchers to make test expectation
statements simpler to write and easier to understand.

### HeaderValueOf

Tests that a HeaderMap argument contains exactly one header with the given key,
whose value satisfies the given expectation. The expectation can be a matcher,
or a string that the value should equal.

Examples:

```cpp
EXPECT_THAT(response->headers(), HeaderValueOf(Headers::get().Server, "envoy"));
```

```cpp
using testing::HasSubstr;
EXPECT_THAT(request->headers(),
            HeaderValueOf(Headers::get().AcceptEncoding, HasSubstr("gzip")));
```

### HttpStatusIs

Tests that a HeaderMap argument has the provided HTTP status code. The status
code can be passed in as a string or an integer.

Example:

```cpp
EXPECT_THAT(response->headers(), HttpStatusIs("200"));
```

### HeaderMapEqual and HeaderMapEqualRef

`HeaderMapEqualRef` tests that two `HeaderMap` arguments are equal.
`HeaderMapEqual` is the same, but it compares two pointers to `HeaderMap`s, and
the matcher's argument must be a `HeaderMapPtr`.

Example:

```cpp
EXPECT_THAT(response->headers(), HeaderMapEqualRef(expected_headers));
```

### ProtoEq, ProtoEqIgnoringField, RepeatedProtoEq

Tests equality of protobufs, with a variant that ignores the value (including
presence) of a single named field. Another variant can be used to compare two
instances of Protobuf::RepeatedPtrField element-by-element.

Example:

```cpp
envoy::service::discovery::v3::DeltaDiscoveryRequest expected_request;
// (not shown: set some fields of expected_request...)
EXPECT_CALL(async_stream_, sendMessage(ProtoEqIgnoringField(expected_request, "response_nonce"), false));

response->mutable_resources()->Add();
response->mutable_resources()->Add();
response->mutable_resources()->Add();
// (not shown: do something to populate those empty added items...)
EXPECT_CALL(callbacks_, onConfigUpdate(RepeatedProtoEq(response->resources()), version));
```

### IsSubsetOfHeaders and IsSupersetOfHeaders

Tests that one `HeaderMap` argument contains every header in another
`HeaderMap`.

Examples:

```cpp
EXPECT_THAT(response->headers(), IsSubsetOfHeaders(allowed_headers));
EXPECT_THAT(response->headers(), IsSupersetOfHeaders(required_headers));
```

## Controlling time in tests

In Envoy production code, time and timers are managed via
[`Event::TimeSystem`](https://github.com/envoyproxy/envoy/blob/main/include/envoy/event/timer.h),
which provides a mechanism for querying the time and setting up time-based
callbacks. Bypassing this abstraction in Envoy code is flagged as a format
violation in CI.

In tests we use a derivation
[`Event::TestTimeSystem`](test_common/test_time_system.h) which adds the ability
to sleep or do a blocking timed wait on a condition variable. There are two
implementations of the `Event::TestTimeSystem` interface:
`Event::TestRealTimeSystem`, and `Event::SimulatedTimeSystem`. The latter is
recommended for all new tests, as it helps avoid flaky tests on slow machines,
and makes tests run faster.

Typically we do not want to have both real-time and simulated-time in the same
test; that could lead to hard-to-reproduce results. Thus both implementations
have a mechanism to enforce that only one of them can be instantiated at once.
A runtime assertion occurs if an `Event::TestRealTimeSystem` and
`Event::SimulatedTimeSystem` are instantiated at the same time. Once the
time-system goes out of scope, usually at the end of a test method, the slate
is clean and a new test-method can use a different time system.

There is also `Event::GlobalTimeSystem`, which can be instantiated in shared
test infrastructure that wants to be agnostic to which `TimeSystem` is used in a
test. When no `TimeSystem` is instantiated in a test, the `Event::GlobalTimeSystem`
will lazy-initialize itself into a concrete `TimeSystem`. Currently this is
`TestRealTimeSystem` but will be changed in the future to `SimulatedTimeSystem`.


## Benchmark tests

Envoy uses [Google Benchmark](https://github.com/google/benchmark/) for
microbenchmarks. There are custom bazel rules, `envoy_cc_benchmark_binary` and
`envoy_benchmark_test`, to execute them locally and in CI environments
respectively. `envoy_benchmark_test` rules call the benchmark binary from a
[script](https://github.com/envoyproxy/envoy/blob/main/bazel/test_for_benchmark_wrapper.sh)
which runs the benchmark with a minimal number of iterations and skipping
expensive benchmarks to quickly verify that the binary is able to run to
completion. In order to collect meaningful bechmarks, `bazel run -c opt` the
benchmark binary target on a quiescent machine.

If you would like to detect when your benchmark test is running under the
wrapper, call
[`Envoy::benchmark::skipExpensiveBechmarks()`](https://github.com/envoyproxy/envoy/blob/main/test/benchmark/main.h).
