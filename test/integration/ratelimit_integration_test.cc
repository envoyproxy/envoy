#include "envoy/service/ratelimit/v2/rls.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// TODO(junr03): legacy rate limit is deprecated. Go back to having only one
// GrpcClientIntegrationParamTest after 1.7.0.
class RatelimitGrpcClientIntegrationParamTest
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, Grpc::ClientType, bool>> {
public:
  ~RatelimitGrpcClientIntegrationParamTest() {}
  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }
  bool useDataPlaneProto() const { return std::get<2>(GetParam()); }
};

class RatelimitIntegrationTest : public HttpIntegrationTest,
                                 public RatelimitGrpcClientIntegrationParamTest {
public:
  RatelimitIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void SetUp() override { initialize(); }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_));
  }

  void initialize() override {
    config_helper_.addFilter(
        "{ name: envoy.rate_limit, config: { domain: some_domain, timeout: 0.5s } }");
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* ratelimit_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ratelimit_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ratelimit_cluster->set_name("ratelimit");
      ratelimit_cluster->mutable_http2_protocol_options();
      setGrpcService(*bootstrap.mutable_rate_limit_service()->mutable_grpc_service(), "ratelimit",
                     fake_upstreams_.back()->localAddress());
    });
    config_helper_.addConfigModifier(
        [](envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager&
               hcm) {
          auto* rate_limit = hcm.mutable_route_config()
                                 ->mutable_virtual_hosts(0)
                                 ->mutable_routes(0)
                                 ->mutable_route()
                                 ->add_rate_limits();
          rate_limit->add_actions()->mutable_destination_cluster();
        });
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      bootstrap.mutable_rate_limit_service()->set_use_data_plane_proto(useDataPlaneProto());
    });
    HttpIntegrationTest::initialize();
  }

  void initiateClientConnection() {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestHeaderMapImpl headers{{":method", "POST"},       {":path", "/test/long/url"},
                                    {":scheme", "http"},       {":authority", "host"},
                                    {"x-lyft-user-id", "123"}, {"x-forwarded-for", "10.0.0.1"}};
    response_ = codec_client_->makeRequestWithBody(headers, request_size_);
  }

  void waitForRatelimitRequest() {
    fake_ratelimit_connection_ = fake_upstreams_[1]->waitForHttpConnection(*dispatcher_);
    ratelimit_request_ = fake_ratelimit_connection_->waitForNewStream(*dispatcher_);
    envoy::service::ratelimit::v2::RateLimitRequest request_msg;
    ratelimit_request_->waitForGrpcMessage(*dispatcher_, request_msg);
    ratelimit_request_->waitForEndStream(*dispatcher_);
    EXPECT_STREQ("POST", ratelimit_request_->headers().Method()->value().c_str());
    if (useDataPlaneProto()) {
      EXPECT_STREQ("/envoy.service.ratelimit.v2.RateLimitService/ShouldRateLimit",
                   ratelimit_request_->headers().Path()->value().c_str());
    } else {
      EXPECT_STREQ("/pb.lyft.ratelimit.RateLimitService/ShouldRateLimit",
                   ratelimit_request_->headers().Path()->value().c_str());
    }
    EXPECT_STREQ("application/grpc", ratelimit_request_->headers().ContentType()->value().c_str());

    envoy::service::ratelimit::v2::RateLimitRequest expected_request_msg;
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

  void sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code code) {
    ratelimit_request_->startGrpcStream();
    envoy::service::ratelimit::v2::RateLimitResponse response_msg;
    response_msg.set_overall_code(code);
    ratelimit_request_->sendGrpcMessage(response_msg);
    ratelimit_request_->finishGrpcStream(Grpc::Status::Ok);
  }

  void cleanup() {
    if (fake_ratelimit_connection_ != nullptr) {
      fake_ratelimit_connection_->close();
      fake_ratelimit_connection_->waitForDisconnect();
    }
    cleanupUpstreamAndDownstream();
  }

  FakeHttpConnectionPtr fake_ratelimit_connection_;
  FakeStreamPtr ratelimit_request_;
  IntegrationStreamDecoderPtr response_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
};

INSTANTIATE_TEST_CASE_P(IpVersionsClientType, RatelimitIntegrationTest,
                        RATELIMIT_GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(RatelimitIntegrationTest, Ok) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OK);
  waitForSuccessfulUpstreamResponse();
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimit) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT);
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
  switch (clientType()) {
  case Grpc::ClientType::EnvoyGrpc:
    test_server_->waitForCounterGe("cluster.ratelimit.upstream_rq_timeout", 1);
    test_server_->waitForCounterGe("cluster.ratelimit.upstream_rq_504", 1);
    EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_timeout")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.ratelimit.upstream_rq_504")->value());
    break;
  case Grpc::ClientType::GoogleGrpc:
    test_server_->waitForCounterGe("grpc.ratelimit.streams_closed_4", 1);
    EXPECT_EQ(1, test_server_->counter("grpc.ratelimit.streams_total")->value());
    EXPECT_EQ(1, test_server_->counter("grpc.ratelimit.streams_closed_4")->value());
    break;
  default:
    NOT_REACHED;
  }
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
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
