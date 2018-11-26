# Envoy tests

Envoy uses the [Google Test framework](https://github.com/google/googletest) for
tests, including
[Google Mock](https://github.com/google/googletest/blob/master/googlemock/README.md) for
mocks and matchers. See the documentation on those pages for information on the
various classes, macros, and matchers that Envoy uses from those frameworks.

## Integration tests

Envoy contains an integration testing framework, for testing
downstream-Envoy-upstream communication.
[See the framework's README for more information.](https://github.com/envoyproxy/envoy/blob/master/test/integration/README.md)

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

### IsSubsetOfHeaders

Tests that one `HeaderMap` argument contains every header in another
`HeaderMap`.

Example:

```cpp
EXPECT_THAT(response->headers(), IsSubsetOfHeaders(required_headers));
```
