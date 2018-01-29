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
config_helper_.addConfigModifier([&](envoy::api::v2::Bootstrap& bootstrap) -> void {
  bootstrap.mutable_runtime()->set_symlink_root(TestEnvironment::temporaryPath("runtime");
});
```

An example of modifying `HttpConnectionManager` to change Envoy’s HTTP/1.1 processing:
```c++
config_helper_.addConfigModifier([&](envoy::api::v2::filter::HttpConnectionManager& hcm) -> void {
  envoy::api::v2::Http1ProtocolOptions options;
  options.mutable_allow_absolute_url()->set_value(true);
  hcm.mutable_http_protocol_options()->CopyFrom(options);
};);
```
An example of modifying `HttpConnectionManager` to add an additional upstream
cluster:
```c++
   config_helper_.addConfigModifier([](envoy::api::v2::Bootstrap& bootstrap) {
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
