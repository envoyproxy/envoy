#include "envoy/service/ratelimit/v2/rls.pb.h"

#include "common/buffer/zero_copy_input_stream_impl.h"
#include "common/grpc/codec.h"
#include "common/grpc/common.h"

#include "extensions/filters/http/ratelimit/config.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Tests Ratelimit functionality with config in filter.
class RatelimitIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  RatelimitIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void SetUp() override { initialize(); }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    fake_upstreams_.emplace_back(
        new FakeUpstream(0, FakeHttpConnection::Type::HTTP2, version_, timeSystem()));
  }

  void initialize() override {

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* ratelimit_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ratelimit_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ratelimit_cluster->set_name("ratelimit");
      ratelimit_cluster->mutable_http2_protocol_options();

      // enhance rate limit filter config based on the configuration of test.
      TestUtility::loadFromYaml(base_filter_config_, proto_config_);
      proto_config_.set_failure_mode_deny(failure_mode_deny_);
      setGrpcService(*proto_config_.mutable_rate_limit_service()->mutable_grpc_service(),
                     "ratelimit", fake_upstreams_.back()->localAddress());

      envoy::api::v2::listener::Filter ratelimit_filter;
      ratelimit_filter.set_name("envoy.rate_limit");
      ProtobufWkt::Struct ratelimit_config = ProtobufWkt::Struct();
      TestUtility::jsonConvert(proto_config_, ratelimit_config);
      ratelimit_filter.mutable_config()->MergeFrom(ratelimit_config);
      config_helper_.addFilter(MessageUtil::getJsonStringFromMessage(ratelimit_filter));
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
    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ratelimit_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_ratelimit_connection_->waitForNewStream(*dispatcher_, ratelimit_request_);
    RELEASE_ASSERT(result, result.message());
    envoy::service::ratelimit::v2::RateLimitRequest request_msg;
    result = ratelimit_request_->waitForGrpcMessage(*dispatcher_, request_msg);
    RELEASE_ASSERT(result, result.message());
    result = ratelimit_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());
    EXPECT_EQ("POST", ratelimit_request_->headers().Method()->value().getStringView());
    EXPECT_EQ("/envoy.service.ratelimit.v2.RateLimitService/ShouldRateLimit",
              ratelimit_request_->headers().Path()->value().getStringView());
    EXPECT_EQ("application/grpc",
              ratelimit_request_->headers().ContentType()->value().getStringView());

    envoy::service::ratelimit::v2::RateLimitRequest expected_request_msg;
    expected_request_msg.set_domain("some_domain");
    auto* entry = expected_request_msg.add_descriptors()->add_entries();
    entry->set_key("destination_cluster");
    entry->set_value("cluster_0");
    EXPECT_EQ(expected_request_msg.DebugString(), request_msg.DebugString());
  }

  void waitForSuccessfulUpstreamResponse() {
    AssertionResult result =
        fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_);
    RELEASE_ASSERT(result, result.message());
    result = fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_);
    RELEASE_ASSERT(result, result.message());
    result = upstream_request_->waitForEndStream(*dispatcher_);
    RELEASE_ASSERT(result, result.message());

    upstream_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(response_size_, true);
    response_->waitForEndStream();

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(request_size_, upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_EQ("200", response_->headers().Status()->value().getStringView());
    EXPECT_EQ(response_size_, response_->body().size());
  }

  void waitForFailedUpstreamResponse(uint32_t response_code) {
    response_->waitForEndStream();
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(std::to_string(response_code),
              response_->headers().Status()->value().getStringView());
  }

  void sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code code,
                             const Http::HeaderMapImpl& response_headers_to_add,
                             const Http::HeaderMapImpl& request_headers_to_add) {
    ratelimit_request_->startGrpcStream();
    envoy::service::ratelimit::v2::RateLimitResponse response_msg;
    response_msg.set_overall_code(code);

    response_headers_to_add.iterate(
        [](const Http::HeaderEntry& h, void* context) -> Http::HeaderMap::Iterate {
          auto header = static_cast<envoy::service::ratelimit::v2::RateLimitResponse*>(context)
                            ->mutable_headers()
                            ->Add();
          header->set_key(std::string(h.key().getStringView()));
          header->set_value(std::string(h.value().getStringView()));
          return Http::HeaderMap::Iterate::Continue;
        },
        &response_msg);
    request_headers_to_add.iterate(
        [](const Http::HeaderEntry& h, void* context) -> Http::HeaderMap::Iterate {
          auto header = static_cast<envoy::service::ratelimit::v2::RateLimitResponse*>(context)
                            ->mutable_request_headers_to_add()
                            ->Add();
          header->set_key(std::string(h.key().getStringView()));
          header->set_value(std::string(h.value().getStringView()));
          return Http::HeaderMap::Iterate::Continue;
        },
        &response_msg);
    ratelimit_request_->sendGrpcMessage(response_msg);
    ratelimit_request_->finishGrpcStream(Grpc::Status::Ok);
  }

  void cleanup() {
    if (fake_ratelimit_connection_ != nullptr) {
      if (clientType() != Grpc::ClientType::GoogleGrpc) {
        // TODO(htuch) we should document the underlying cause of this difference and/or fix it.
        AssertionResult result = fake_ratelimit_connection_->close();
        RELEASE_ASSERT(result, result.message());
      }
      AssertionResult result = fake_ratelimit_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  void basicFlow() {
    initiateClientConnection();
    waitForRatelimitRequest();
    sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OK,
                          Http::HeaderMapImpl{}, Http::HeaderMapImpl{});
    waitForSuccessfulUpstreamResponse();
    cleanup();

    EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
    EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
    EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
  }

  FakeHttpConnectionPtr fake_ratelimit_connection_;
  FakeStreamPtr ratelimit_request_;
  IntegrationStreamDecoderPtr response_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
  bool failure_mode_deny_ = false;
  envoy::config::filter::http::rate_limit::v2::RateLimit proto_config_{};
  const std::string base_filter_config_ = R"EOF(
    domain: some_domain
    timeout: 0.5s
  )EOF";
};

// Test that verifies failure mode cases.
class RatelimitFailureModeIntegrationTest : public RatelimitIntegrationTest {
public:
  RatelimitFailureModeIntegrationTest() { failure_mode_deny_ = true; }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, RatelimitIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, RatelimitFailureModeIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

TEST_P(RatelimitIntegrationTest, Ok) { basicFlow(); }

TEST_P(RatelimitIntegrationTest, OkWithHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();
  Http::TestHeaderMapImpl ratelimit_response_headers{{"x-ratelimit-limit", "1000"},
                                                     {"x-ratelimit-remaining", "500"}};
  Http::TestHeaderMapImpl request_headers_to_add{{"x-ratelimit-done", "true"}};

  sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OK,
                        ratelimit_response_headers, request_headers_to_add);
  waitForSuccessfulUpstreamResponse();

  ratelimit_response_headers.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value(), response->headers().get(lower_key)->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      },
      response_.get());

  request_headers_to_add.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        FakeStream* upstream = static_cast<FakeStream*>(context);
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value(), upstream->headers().get(lower_key)->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      },
      upstream_request_.get());

  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimit) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT,
                        Http::HeaderMapImpl{}, Http::HeaderMapImpl{});
  waitForFailedUpstreamResponse(429);
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimitWithHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();
  Http::TestHeaderMapImpl ratelimit_response_headers{
      {"x-ratelimit-limit", "1000"}, {"x-ratelimit-remaining", "0"}, {"retry-after", "33"}};
  sendRateLimitResponse(envoy::service::ratelimit::v2::RateLimitResponse_Code_OVER_LIMIT,
                        ratelimit_response_headers, Http::HeaderMapImpl{});
  waitForFailedUpstreamResponse(429);

  ratelimit_response_headers.iterate(
      [](const Http::HeaderEntry& entry, void* context) -> Http::HeaderMap::Iterate {
        IntegrationStreamDecoder* response = static_cast<IntegrationStreamDecoder*>(context);
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value(), response->headers().get(lower_key)->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      },
      response_.get());

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
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.failure_mode_allowed")->value());
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
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

TEST_P(RatelimitIntegrationTest, ConnectImmediateDisconnect) {
  initiateClientConnection();
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ratelimit_connection_));
  ASSERT_TRUE(fake_ratelimit_connection_->close());
  ASSERT_TRUE(fake_ratelimit_connection_->waitForDisconnect(true));
  fake_ratelimit_connection_ = nullptr;
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

TEST_P(RatelimitIntegrationTest, FailedConnect) {
  // Do not reset the fake upstream for the ratelimiter, but have it stop listening.
  // If we reset, the Envoy will continue to send H2 to the original rate limiter port, which may
  // be used by another test, and data sent to that port "unexpectedly" will cause problems for
  // that test.
  fake_upstreams_[1]->cleanUp();
  initiateClientConnection();
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse();
  cleanup();
}

TEST_P(RatelimitFailureModeIntegrationTest, ErrorWithFailureModeOff) {
  initiateClientConnection();
  waitForRatelimitRequest();
  ratelimit_request_->encodeHeaders(Http::TestHeaderMapImpl{{":status", "503"}}, true);
  // Rate limiter fail closed
  waitForFailedUpstreamResponse(500);
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.error")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.failure_mode_allowed"));
}

} // namespace
} // namespace Envoy
