#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace {

class DynamicModuleBridgeIntegrationTest : public HttpProtocolIntegrationTest {
public:
  DynamicModuleBridgeIntegrationTest() { enableHalfClose(true); }

  void initialize() override {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          hcm.mutable_delayed_close_timeout()->set_seconds(1);
          hcm.mutable_stream_idle_timeout()->set_seconds(5);

          auto* route_config = hcm.mutable_route_config();
          ASSERT_EQ(1, route_config->virtual_hosts_size());
          route_config->mutable_virtual_hosts(0)->clear_domains();
          route_config->mutable_virtual_hosts(0)->add_domains("*");
        });
    HttpIntegrationTest::initialize();
  }

  void initializeWithBridgeConfig(const std::string& bridge_mode) {
    TestEnvironment::setEnvVar(
        "ENVOY_DYNAMIC_MODULES_SEARCH_PATH",
        TestEnvironment::substitute(
            "{{ test_rundir }}/test/extensions/dynamic_modules/test_data/rust"),
        1);

    config_helper_.addConfigModifier(
        [bridge_mode](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
          cluster->mutable_upstream_config()->set_name("envoy.upstreams.http.dynamic_modules");
          cluster->mutable_upstream_config()->mutable_typed_config()->set_type_url(
              "type.googleapis.com/"
              "envoy.extensions.upstreams.http.dynamic_modules.v3.Config");

          envoy::extensions::upstreams::http::dynamic_modules::v3::Config proto_config;
          auto* dm_config = proto_config.mutable_dynamic_module_config();
          dm_config->set_name("upstream_http_tcp_bridge");
          dm_config->set_do_not_close(true);

          proto_config.set_bridge_name("test_bridge");

          // Pass bridge mode as config.
          Protobuf::StringValue config_value;
          config_value.set_value(bridge_mode);
          std::ignore = proto_config.mutable_bridge_config()->PackFrom(config_value);

          std::ignore =
              cluster->mutable_upstream_config()->mutable_typed_config()->PackFrom(proto_config);
        });

    initialize();
  }

  void setupConnection(bool header_only = false) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder = codec_client_->startRequest(request_headers_, header_only);
    request_encoder_ = &encoder_decoder.first;
    response_ = std::move(encoder_decoder.second);
    ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_raw_upstream_connection_));
  }

  // Caps each upstream connection at one request and runs two requests. Releasing a connection
  // then drains its client, so the pool closes it before the bridge would. Each request still gets
  // its reply on a connection of its own, Envoy closes each connection once, and no close counts
  // against a request still active.
  void runOneRequestPerConnection(const std::string& bridge_mode) {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      ConfigHelper::HttpProtocolOptions protocol_options;
      protocol_options.mutable_common_http_protocol_options()
          ->mutable_max_requests_per_connection()
          ->set_value(1);
      ConfigHelper::setProtocolOptions(*bootstrap.mutable_static_resources()->mutable_clusters(0),
                                       protocol_options);
    });
    initializeWithBridgeConfig(bridge_mode);
    codec_client_ = makeHttpConnection(lookupPort("http"));

    for (const std::string reply : {"first_reply", "second_reply"}) {
      auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
      FakeRawConnectionPtr upstream_connection;
      ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));
      ASSERT_TRUE(
          upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
      ASSERT_TRUE(upstream_connection->write(reply));
      ASSERT_TRUE(response->waitForEndStream());
      EXPECT_EQ(reply, response->body());
      ASSERT_TRUE(upstream_connection->waitForHalfClose());
    }
    ASSERT_NO_FATAL_FAILURE(test_server_->waitForCounter(
        "cluster.cluster_0.upstream_cx_destroy_local", testing::Eq(2)));
    EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_cx_max_requests")->value());
    EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
    Stats::CounterSharedPtr closes_with_active_request =
        test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq");
    EXPECT_EQ(0, closes_with_active_request->value());
  }

  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":authority", "test.host.com:80"}, {":path", "/"}};

  FakeRawConnectionPtr fake_raw_upstream_connection_;
  IntegrationStreamDecoderPtr response_;
};

INSTANTIATE_TEST_SUITE_P(HttpAndIpVersions, DynamicModuleBridgeIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1}, {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(DynamicModuleBridgeIntegrationTest, StreamingMode) {
  initializeWithBridgeConfig("streaming");
  setupConnection();

  // Send first data chunk.
  codec_client_->sendData(*request_encoder_, "chunk1", false);

  // The streaming bridge prepends "METHOD=POST " during encode_headers.
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST ")));

  // Send upstream TCP response.
  ASSERT_TRUE(fake_raw_upstream_connection_->write("tcp_response_data"));

  // Verify response headers are received.
  response_->waitForHeaders();
  EXPECT_EQ("200", response_->headers().getStatusValue());

  // Send end of stream.
  codec_client_->sendData(*request_encoder_, "chunk2", true);
  ASSERT_TRUE(
      fake_raw_upstream_connection_->waitForData(FakeRawConnection::waitForInexactMatch("chunk2")));

  // Close upstream connection to end the response.
  ASSERT_TRUE(fake_raw_upstream_connection_->close());

  // Wait for response completion.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_EQ("tcp_response_data", response_->body());
}

TEST_P(DynamicModuleBridgeIntegrationTest, LocalReplyMode) {
  initializeWithBridgeConfig("local_reply");

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("403", response->headers().getStatusValue());
  EXPECT_EQ("access denied", response->body());
}

// Regression test for a bridge use after free. The module sends a complete response during
// encode_headers while the downstream request is still open, so the router resets the upstream and
// destroys the bridge synchronously. The response is now applied after the hook returns, so the
// bridge is never torn down under a live module borrow.
TEST_P(DynamicModuleBridgeIntegrationTest, LocalReplyBeforeDownstreamEndStream) {
  initializeWithBridgeConfig("local_reply");

  setupConnection();
  ASSERT_TRUE(response_->waitForEndStream());

  EXPECT_EQ("403", response_->headers().getStatusValue());
  EXPECT_EQ("access denied", response_->body());
}

// The module ends each response on the first upstream reply while the upstream connection stays
// open, so the router finishes both requests without a reset. The bridge used to return that
// connection to the pool, so the second request wrote into the connection the first had
// half-closed, or read bytes the upstream sent after the first reply. Each request now gets its own
// upstream connection, which Envoy closes when the request ends, with no request still active on
// it. The first request half-closes the upstream connection and the second does not.
TEST_P(DynamicModuleBridgeIntegrationTest, EachRequestUsesItsOwnUpstreamConnection) {
  initializeWithBridgeConfig("request_response");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto first_response = codec_client_->makeRequestWithBody(request_headers_, "first_request");
  FakeRawConnectionPtr first_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(first_upstream_connection));
  ASSERT_TRUE(first_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("first_request")));
  ASSERT_TRUE(first_upstream_connection->waitForHalfClose());
  ASSERT_TRUE(first_upstream_connection->write("first_reply"));
  ASSERT_TRUE(first_response->waitForEndStream());
  EXPECT_EQ("first_reply", first_response->body());
  ASSERT_NO_FATAL_FAILURE(
      test_server_->waitForCounter("cluster.cluster_0.upstream_cx_destroy_local", testing::Eq(1)));

  auto second_response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  FakeRawConnectionPtr second_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(second_upstream_connection));
  ASSERT_TRUE(second_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
  ASSERT_TRUE(second_upstream_connection->write("second_reply"));
  ASSERT_TRUE(second_response->waitForEndStream());
  EXPECT_EQ("second_reply", second_response->body());
  // The fake upstream enables half-close, so it sees Envoy's close as a half-close.
  ASSERT_TRUE(second_upstream_connection->waitForHalfClose());
  ASSERT_NO_FATAL_FAILURE(
      test_server_->waitForCounter("cluster.cluster_0.upstream_cx_destroy_local", testing::Eq(2)));
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq")->value());
}

// The upstream closes before it replies, so the router resets the request inside the pool's close
// callback and destroys the bridge there, after the bridge recorded the close. The reply and the
// counters show the reset, and the unit test DestroyAfterACloseEventLeavesTheConnectionAlone pins
// that the destructor reads nothing of the closed connection.
TEST_P(DynamicModuleBridgeIntegrationTest, UpstreamCloseBeforeTheReplyResetsTheRequest) {
  initializeWithBridgeConfig("request_response");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));
  ASSERT_TRUE(
      upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
  ASSERT_TRUE(upstream_connection->close());
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("503", response->headers().getStatusValue());
  ASSERT_NO_FATAL_FAILURE(test_server_->waitForCounter(
      "cluster.cluster_0.upstream_cx_destroy_remote_with_active_rq", testing::Eq(1)));
  EXPECT_EQ(0, test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local")->value());
}

// The module enables reuse each time it ends a response on the upstream's reply, so header-only
// requests share one upstream connection. A request with a body ends its upstream data, which
// half-closes the connection, so Envoy closes it once that request ends, reuse or not.
TEST_P(DynamicModuleBridgeIntegrationTest, ModuleEnabledReuseSharesTheUpstreamConnection) {
  initializeWithBridgeConfig("request_response_reuse");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto first_response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));
  ASSERT_TRUE(
      upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
  ASSERT_TRUE(upstream_connection->write("first_reply"));
  ASSERT_TRUE(first_response->waitForEndStream());
  EXPECT_EQ("first_reply", first_response->body());

  auto second_response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  ASSERT_TRUE(upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST METHOD=POST ")));
  ASSERT_TRUE(upstream_connection->write("second_reply"));
  ASSERT_TRUE(second_response->waitForEndStream());
  EXPECT_EQ("second_reply", second_response->body());

  auto third_response = codec_client_->makeRequestWithBody(request_headers_, "third_request");
  ASSERT_TRUE(
      upstream_connection->waitForData(FakeRawConnection::waitForInexactMatch("third_request")));
  ASSERT_TRUE(upstream_connection->waitForHalfClose());
  ASSERT_TRUE(upstream_connection->write("third_reply"));
  ASSERT_TRUE(third_response->waitForEndStream());
  EXPECT_EQ("third_reply", third_response->body());
  ASSERT_NO_FATAL_FAILURE(
      test_server_->waitForCounter("cluster.cluster_0.upstream_cx_destroy_local", testing::Eq(1)));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
  EXPECT_EQ(
      0,
      test_server_->counter("cluster.cluster_0.upstream_cx_destroy_local_with_active_rq")->value());
}

// The upstream replies while the downstream request is still open, so the router resets the
// upstream request although the module enabled reuse, and Envoy resets the connection, which the
// fake upstream sees as a disconnect, where a FIN would show as a half-close. The next request
// opens a new one.
TEST_P(DynamicModuleBridgeIntegrationTest, ReplyBeforeTheRequestEndsClosesAReusableConnection) {
  initializeWithBridgeConfig("request_response_reuse");
  setupConnection();
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
  ASSERT_TRUE(fake_raw_upstream_connection_->write("early_reply"));
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_EQ("early_reply", response_->body());
  ASSERT_TRUE(fake_raw_upstream_connection_->waitForDisconnect());
  ASSERT_NO_FATAL_FAILURE(
      test_server_->waitForCounter("cluster.cluster_0.upstream_cx_destroy_local", testing::Eq(1)));
  codec_client_->close();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto second_response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  FakeRawConnectionPtr second_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(second_upstream_connection));
  ASSERT_TRUE(second_upstream_connection->waitForData(
      FakeRawConnection::waitForInexactMatch("METHOD=POST ")));
  ASSERT_TRUE(second_upstream_connection->write("second_reply"));
  ASSERT_TRUE(second_response->waitForEndStream());
  EXPECT_EQ("second_reply", second_response->body());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.upstream_cx_total")->value());
}

// The module sends a request larger than a socket takes in one write, ends its upstream data and
// replies at once, as a fire-and-forget protocol does, so the bridge is destroyed while most of the
// request is still queued in Envoy. Envoy keeps the connection until all of it is written, so the
// upstream reads the whole request before the half-close instead of a cut one. The fake upstream
// reads nothing until the test claims the connection after the reply, so a close that wrote only
// one pass would stop at what the socket buffers take, while the drain waits for the claim, which
// must come within the drain's one-second bound of real time.
TEST_P(DynamicModuleBridgeIntegrationTest, ReplyBeforeTheUpstreamReadsTheRequestDeliversItWhole) {
  initializeWithBridgeConfig("fire_and_forget");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("sent", response->body());
  FakeRawConnectionPtr upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(upstream_connection));
  constexpr uint32_t request_bytes = 8 * 1024 * 1024;
  std::string request;
  ASSERT_TRUE(upstream_connection->waitForData(
      FakeRawConnection::waitForAtLeastBytes(request_bytes), &request));
  EXPECT_EQ(request_bytes, request.size());
  EXPECT_EQ(std::string::npos, request.find_first_not_of('x'));
  ASSERT_TRUE(upstream_connection->waitForHalfClose());
  // The drained connection still held its request when it closed.
  ASSERT_NO_FATAL_FAILURE(test_server_->waitForCounter(
      "cluster.cluster_0.upstream_cx_destroy_local_with_active_rq", testing::Eq(1)));
}

// The fake upstream never reads, so the drain is still waiting when Envoy shuts down while the fake
// upstream is still up. The pool's teardown then closes the connection, which must still reach and
// delete the drainer.
TEST_P(DynamicModuleBridgeIntegrationTest, DrainInProgressAtShutdownIsReaped) {
  initializeWithBridgeConfig("fire_and_forget");
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("200", response->headers().getStatusValue());
  codec_client_->close();
  test_server_.reset();
}

TEST_P(DynamicModuleBridgeIntegrationTest, OneRequestPerConnectionWithoutReuse) {
  ASSERT_NO_FATAL_FAILURE(runOneRequestPerConnection("request_response"));
}

TEST_P(DynamicModuleBridgeIntegrationTest, OneRequestPerConnectionWithReuse) {
  ASSERT_NO_FATAL_FAILURE(runOneRequestPerConnection("request_response_reuse"));
}

// The bridge loads its module when the cluster is created, so a missing module rejects the cluster,
// which here is a static cluster of the bootstrap.
TEST_P(DynamicModuleBridgeIntegrationTest, ModuleLoadFailureRejectsTheCluster) {
  TestEnvironment::setEnvVar("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", "/should/not/find/this/path", 1);

  config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->mutable_upstream_config()->set_name("envoy.upstreams.http.dynamic_modules");
    cluster->mutable_upstream_config()->mutable_typed_config()->set_type_url(
        "type.googleapis.com/"
        "envoy.extensions.upstreams.http.dynamic_modules.v3.Config");

    envoy::extensions::upstreams::http::dynamic_modules::v3::Config proto_config;
    proto_config.mutable_dynamic_module_config()->set_name("nonexistent_module");
    proto_config.set_bridge_name("test_bridge");
    std::ignore =
        cluster->mutable_upstream_config()->mutable_typed_config()->PackFrom(proto_config);
  });

  EXPECT_DEATH(initialize(), "nonexistent_module");
}

} // namespace
} // namespace Envoy
