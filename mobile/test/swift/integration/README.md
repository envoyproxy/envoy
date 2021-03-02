This test suite tests end-to-end integration from the platform layer to the core layer's HTTP
functionality. It tests both the request side `send{Headers|Data}`, `close`, `cancel`, as
well as the response
side via the ` setOnResponse{...}` functions.

TODO: These tests are broken apart into different suites and bazel targets in order to tear down
app state -- and thus static lifetime objects like the Envoy engine -- between tests. When
multiple engine support (https://github.com/lyft/envoy-mobile/issues/332) lands, all of these
tests can be collapsed to the same suite/target.

TODO: setOnTrailers is not tested as the neither the `direct_response` pathway, nor the router
allow sending trailers programmatically. Add tests once possible.
