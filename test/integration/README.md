# Writing new tests

The Envoy integration test framework is designed to make it simple to test downstream-Envoy-upstream
communication. In the common case, one

- Sends a request from downstream through Envoy
- Verifies the request is received upstream, and possibly inspects elements of the headers or body
- Sends a response from upstream through Envoy
- Verifies the request is received downstream, again possibly inspecting headers and body

For the simplest variant of this, one could do the following.

```c++
// start Envoy, set up the fake upstreams.
initialize();

// Create a client aimed at Envoy’s default HTTP port.
codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));

// Create some request headers.
Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                        {":path", "/test/long/url"},
                                        {":scheme", "http"},
                                        {":authority", "host"}};

// Send the request headers from the client, wait until they are received upstream. When they
// are received, send the default response headers from upstream and wait until they are
// received at by client
sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

// Verify the proxied request was received upstream, as expected.
EXPECT_TRUE(upstream_request_->complete());
EXPECT_EQ(0U, upstream_request_->bodyLength());
// Verify the proxied response was received downstream, as expected.
EXPECT_TRUE(response_->complete());
EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
EXPECT_EQ(0U, response_->body().size());
```

Once you have the basic end-to-end test, it is fairly straight forward to modify it to test more
interesting corner cases. There are existing tests which send requests with bodies, have
downstream or upstream disconnect or time out, send multiple simultaneous requests on the same
connection, etc. Given that many of the disconnect/timeout cases are covered, a common case for
testing is covering a newly added configuration option.

Most of Envoy's tests have been migrated from using [`json flatfiles`](../config/integration/) to
using a basic configuration defined as `basic_config` in [`config/utility.cc`](../config/utility.cc).
This configuration may be modified before the call to `initialize()`.
The [`ConfigHelper`](../config/utility.h) has utilities for common alterations such as:

```c++
// Set the default protocol to HTTP2
setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
```

or

```c++
// Add a buffering filter on the request path
config_helper_.addFilter(ConfigHelper::DEFAULT_BUFFER_FILTER);
```

For other edits which are less likely reusable, one can add config modifiers. Config modifiers
allow arbitrary modification of Envoy’s configuration just before Envoy is brought up. One can add
a config modifier to alter the bootstrap proto, one which affects the first `HttpConnectionManager`
object in the config, or mix and match. Config modifiers are operated on in the order they are
added, so one could, for example, modify the default `HttpConnectionManager`, duplicate the listening
config, and then change the first `HttpConnectionManager` to be different from the second.

An example of modifying the bootstrap proto to overwrite runtime defaults:

```c++
TestEnvironment::writeStringToFileForTest("runtime/ssl.alt_alpn", "100");
config_helper_.addConfigModifier([&](envoy::config::bootstrap::v2::Bootstrap& bootstrap) -> void {
  bootstrap.mutable_runtime()->set_symlink_root(TestEnvironment::temporaryPath("runtime");
});
```

An example of modifying `HttpConnectionManager` to change Envoy’s HTTP/1.1 processing:

```c++
config_helper_.addConfigModifier([&](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm) -> void {
  envoy::api::v2::core::Http1ProtocolOptions options;
  options.mutable_allow_absolute_url()->set_value(true);
  hcm.mutable_http_protocol_options()->CopyFrom(options);
};);
```

An example of modifying `HttpConnectionManager` to add an additional upstream
cluster:

```c++
   config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      bootstrap.mutable_rate_limit_service()->set_cluster_name("ratelimit");
      auto* ratelimit_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ratelimit_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ratelimit_cluster->set_name("ratelimit");
      ratelimit_cluster->mutable_http2_protocol_options();
    });
```

In addition to the existing test framework, which allows for carefully timed interaction and ordering of events between downstream, Envoy, and Upstream, there is now an “autonomous” framework which simplifies the common case where the timing is not essential (or bidirectional streaming is desired). When AutonomousUpstream is used, by setting `autonomous_upstream_ = true` before `initialize()`, upstream will by default create AutonomousHttpConnections for each incoming connection and AutonomousStreams for each incoming stream. By default, the streams will respond to each complete request with “200 OK” and 10 bytes of payload, but this behavior can be altered by setting various request headers, as documented in [`autonomous_upstream.h`](autonomous_upstream.h)

# Extending the test framework

The Envoy integration test framework is most definitely a work in progress.
When encountering features which do not exist (actions for the autonomous
backend, testing new sections of configuration) please use your best judgement
if the changes will be needed by one specific test file, or will be likely
reused in other integration tests. If it's likely be reused, please add the
appropriate functions to existing utilities or add new test utilities. If it's
likely a one-off change, it can be scoped to the existing test file.


# Deflaking tests

The instructions below assume the developer is running tests natively with bazel
rather than in docker. For developers using docker the best workaround today is
to replace `//test/...` on the relevant `ci/do_ci.sh`with the command lines
referenced below and remember to back those changes out before sending the fix
upstream!

## Reproducing test flakes

The first step of fixing test flakes is reproducing the test flake. In general
if you have written a test which flakes, you can start by running

``
bazel test [test_name] --runs_per_test=1000
``

Which runs the full test many times. If this works, great!  If not, it's worth
trying to stress your system more by running more tests in parallel, by setting
`--jobs` and `--local_resources.`

Once you've managed to reproduce a failure it may be beneficial to limit your
test run to the specific failing test(s) with `--gtest_filter`. This may cause
the test to flake less often (i.e. if two tests are interfering with each other,
scoping to your specific test name may harm rather than help reproducibility.)
but if it works it lets you iterate faster.

Another helpful tip for debugging is turn turn up Envoy trace logs with
`--test_arg="-l trace"`. Again if the test failure is due to a race, this may make
it harder to reproduce, and it may also hide any custom logging you add, but it's a
handy thing to know of to follow the general flow.

The full command might look something like

```
bazel test //test/integration:http2_upstream_integration_test \
--test_arg=--gtest_filter="IpVersions/Http2UpstreamIntegrationTest.RouterRequestAndResponseWithBodyNoBuffer/IPv6" \
--jobs 60 --local_resources 100000000000,100000000000,10000000 --runs_per_test=1000 --test_arg="-l trace"
```

## Debugging test flakes

Once you've managed to reproduce your test flake, you get to figure out what's
going on. If your failure mode isn't documented below, ideally some combination
of cerr << logging and trace logs will help you sort out what is going on (and
please add to this document as you figure it out!)

## Unexpected disconnects

As commented in `HttpIntegrationTest::cleanupUpstreamAndDownstream()`, the
tear-down sequence between upstream, Envoy, and client is somewhat sensitive to
ordering. If a given unit test does not use the provided member variables, for
example opens multiple client or upstream connections, the test author should be
aware of test best practices for clean-up which boil down to "Clean up upstream
first".

Upstream connections run in their own threads, so if the client disconnects with
open streams, there's a race where Envoy detects the disconnect, and kills the
corresponding upstream stream, which is indistinguishable from an unexpected
disconnect and triggers test failure. Because the client is run from the main
thread, if upstream is closed first, the client will not detect the inverse
close, so no test failure will occur.

## Unparented upstream connections

The most common failure mode here is if the test adds additional fake
upstreams for *DS connections (ADS, EDS etc) which are not properly shut down
(for a very sensitive test framework)

The failure mode here is that during test teardown one closes the DS connection
and then shuts down Envoy. Unfortunately as Envoy is running in its own thread,
it will try to re-establish the *DS connection, sometimes creating a connection
which is then "unparented". The solution here is to explicitly allow Envoy
reconnects before closing the connection, using

`my_ds_upstream_->set_allow_unexpected_disconnects(true);`

