#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "source/common/ratelimit/ratelimit.pb.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class RatelimitIntegrationTest : public HttpIntegrationTest,
                                 public testing::TestWithParam<Network::Address::IpVersion> {
public:
  RatelimitIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override {
    HttpIntegrationTest::SetUp();
    initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
    ports_.push_back(fake_upstreams_.back()->localAddress()->ip()->port());
  }

  void initialize() override {
    config_helper_.addFilter(
        "{ name: envoy.rate_limit, config: { deprecated_v1: true, value: { domain: "
        "some_domain, timeout_ms: 500 } } }");
    config_helper_.addConfigModifier([](envoy::api::v2::Bootstrap& bootstrap) {
      bootstrap.mutable_rate_limit_service()->set_cluster_name("ratelimit");
      auto* ratelimit_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ratelimit_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ratelimit_cluster->set_name("ratelimit");
      ratelimit_cluster->mutable_http2_protocol_options();
    });
    config_helper_.addConfigModifier(
        [](envoy::api::v2::filter::network::HttpConnectionManager& hcm) {
          auto* rate_limit = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->add_rate_limits();
          rate_limit->add_actions()->mutable_destination_cluster();
        });
    named_ports_ = {"http"};
    HttpIntegrationTest::initialize();
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestHeaderMapImpl headers{{":method", "POST"},       {":path", "/test/long/url"},
                                    {":scheme", "http"},       {":authority", "host"},
                                    {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
    codec_client_->makeRequestWithBody(headers, request_size_, *response_);
  }

  void waitForRatelimitRequest() {
    fake_ratelimit_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
    ratelimit_request_ = fake_ratelimit_connection_->waitForNewStream(*dispatcher_);
    pb::lyft::ratelimit::RateLimitRequest request_msg;
    ratelimit_request_->waitForGrpcMessage(*dispatcher_, request_msg);
    ratelimit_request_->waitForEndStream(*dispatcher_);
    EXPECT_STREQ("POST", ratelimit_request_->headers().Method()->value().c_str());
    EXPECT_STREQ("/pb.lyft.ratelimit.RateLimitService/ShouldRateLimit",
                 ratelimit_request_->headers().Path()->value().c_str());
    EXPECT_STREQ("application/grpc", ratelimit_request_->headers().ContentType()->value().c_str());

    pb::lyft::ratelimit::RateLimitRequest expected_request_msg;
    expected_request_msg.set_domain("some_domain");
    auto* entry = expected_request_msg.add_descriptors()->add_entries();
    entry->set_key("destination_cluster");
    entry->set_value("cluster_0");
    EXPECT_EQ(expected_request_msg.DebugString(), request_msg.DebugString());
  }

  void waitForSuccessfulUpstreamResponse() {
    fake_upstream_connection_ = fake_upstreams_[0]->waitForHttpConnection(*dispatcher_);
    upstream_request_ = fake_upstream_connection_->waitForNewStream(*dispatcher_);
    upstream_request_->waitForEndStream(*dispatcher_);

    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);
    response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void waitForFailedUpstreamResponse(uint32_t response_code) {
    response_->waitForEndStream();
    EXPECT_TRUE(response_->complete());
    EXPECT_STREQ(std::to_string(response_code).c_str(),
                 response_->headers().Status()->value().c_str());
  }

  void sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code code) {
    ratelimit_request_->startGrpcStream();
    pb::lyft::ratelimit::RateLimitResponse response_msg;
    response_msg.set_overall_code(code);
    ratelimit_request_->sendGrpcMessage(response_msg);
    ratelimit_request_->finishGrpcStream(Grpc::Status::Ok);
  }

  void cleanup() {
    codec_client_->close();
    if (fake_ratelimit_connection_ != nullptr) {
      fake_ratelimit_connection_->close();
      fake_ratelimit_connection_->waitForDisconnect();
    }
    if (fake_upstream_connection_ != nullptr) {
      fake_upstream_connection_->close();
      fake_upstream_connection_->waitForDisconnect();
    }
  }

  FakeHttpConnectionPtr fake_ratelimit_connection_;
  FakeStreamPtr ratelimit_request_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
};

INSTANTIATE_TEST_CASE_P(IpVersions, RatelimitIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(RatelimitIntegrationTest, Ok) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code_OK);
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimit) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(pb::lyft::ratelimit::RateLimitResponse_Code_OVER_LIMIT);
  waitForFailedUpstreamResponse(429);
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, Error) {
  initiateClientConnection();
  waitForRatelimitRequest();
  ratelimit_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "404"}}, true);
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.error")->value());
}

TEST_P(RatelimitIntegrationTest, Timeout) {
  initiateClientConnection();
  waitForRatelimitRequest();
  test_server_->waitForCounterGe("cluster.ratelimit.upstream_rq_timeout", 1);
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_timeout")->value());
  EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_504")->value());
}

TEST_P(RatelimitIntegrationTest, ConnectImmediateDisconnect) {
  initiateClientConnection();
  fake_ratelimit_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
  fake_ratelimit_connection_->close();
  fake_ratelimit_connection_->waitForDisconnect(true);
  fake_ratelimit_connection_ = nullptr;
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

TEST_P(RatelimitIntegrationTest, FailedConnect) {
  fake_upstreams_[1].reset();
  initiateClientConnection();
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

} // namespace
} // namespace Envoy
