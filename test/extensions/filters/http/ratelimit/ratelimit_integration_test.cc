#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.h"
#include "envoy/extensions/filters/http/ratelimit/v3/rate_limit.pb.validate.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/extensions/filters/http/common/ratelimit_headers.h"
#include "source/extensions/filters/http/ratelimit/config.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/extensions/filters/common/ratelimit/utils.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Tests Ratelimit functionality with config in filter.
class RatelimitIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                 public HttpIntegrationTest {
public:
  RatelimitIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, ipVersion()) {
    // TODO(ggreenway): add tag extraction rules.
    skip_tag_extraction_rule_check_ = true;
  }

  void SetUp() override { initialize(); }

  void createUpstreams() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

    // Add autonomous upstream.
    auto endpoint = upstream_address_fn_(0);
    fake_upstreams_.emplace_back(new AutonomousUpstream(
        Network::Test::createRawBufferDownstreamSocketFactory(), endpoint->ip()->port(),
        endpoint->ip()->version(), upstreamConfig(), true));

    // Add ratelimit upstream.
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* ratelimit_cluster = bootstrap.mutable_static_resources()->add_clusters();
      ratelimit_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      ratelimit_cluster->set_name("ratelimit_cluster");
      ConfigHelper::setHttp2(*ratelimit_cluster);

      // enhance rate limit filter config based on the configuration of test.
      TestUtility::loadFromYaml(base_filter_config_, proto_config_);
      proto_config_.set_failure_mode_deny(failure_mode_deny_);
      proto_config_.set_enable_x_ratelimit_headers(enable_x_ratelimit_headers_);
      proto_config_.set_disable_x_envoy_ratelimited_header(disable_x_envoy_ratelimited_header_);
      setGrpcService(*proto_config_.mutable_rate_limit_service()->mutable_grpc_service(),
                     "ratelimit_cluster", fake_upstreams_.back()->localAddress());
      proto_config_.mutable_rate_limit_service()->set_transport_api_version(
          envoy::config::core::v3::ApiVersion::V3);

      envoy::config::listener::v3::Filter ratelimit_filter;
      ratelimit_filter.set_name("envoy.filters.http.ratelimit");
      ratelimit_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(ratelimit_filter));
    });
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
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
    ratelimit_requests_.resize(num_requests_);
    upstream_requests_.resize(num_requests_);
    responses_.resize(num_requests_);
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    for (int i = 0; i < num_requests_; i++) {
      Http::TestRequestHeaderMapImpl headers{
          {":method", "POST"},    {":path", "/test/long/url"}, {":scheme", "http"},
          {":authority", "host"}, {"x-lyft-user-id", "123"},   {"x-forwarded-for", "10.0.0.1"}};
      responses_[i] = codec_client_->makeRequestWithBody(headers, request_size_);
    }
  }

  void waitForRatelimitRequest() {

    AssertionResult result =
        fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ratelimit_connection_);
    RELEASE_ASSERT(result, result.message());
    for (int i = 0; i < num_requests_; i++) {
      AssertionResult result =
          fake_ratelimit_connection_->waitForNewStream(*dispatcher_, ratelimit_requests_[i]);
      RELEASE_ASSERT(result, result.message());
      envoy::service::ratelimit::v3::RateLimitRequest request_msg;
      result = ratelimit_requests_[i]->waitForGrpcMessage(*dispatcher_, request_msg);
      RELEASE_ASSERT(result, result.message());
      result = ratelimit_requests_[i]->waitForEndStream(*dispatcher_);
      RELEASE_ASSERT(result, result.message());
      EXPECT_EQ("POST", ratelimit_requests_[i]->headers().getMethodValue());
      EXPECT_EQ("/envoy.service.ratelimit.v3.RateLimitService/ShouldRateLimit",
                ratelimit_requests_[i]->headers().getPathValue());
      EXPECT_EQ("application/grpc", ratelimit_requests_[i]->headers().getContentTypeValue());

      envoy::service::ratelimit::v3::RateLimitRequest expected_request_msg;
      expected_request_msg.set_domain("some_domain");
      auto* entry = expected_request_msg.add_descriptors()->add_entries();
      entry->set_key("destination_cluster");
      entry->set_value("cluster_0");
      EXPECT_EQ(expected_request_msg.DebugString(), request_msg.DebugString());
    }
  }

  void waitForSuccessfulUpstreamResponse(int request_id) {
    EXPECT_TRUE(responses_[request_id]->waitForEndStream());
    EXPECT_TRUE(responses_[request_id]->complete());
    EXPECT_EQ("200", responses_[request_id]->headers().getStatusValue());
  }

  void waitForFailedUpstreamResponse(uint32_t response_code, int request_id) {
    EXPECT_TRUE(responses_[request_id]->waitForEndStream());
    EXPECT_TRUE(responses_[request_id]->complete());
    EXPECT_EQ(std::to_string(response_code), responses_[request_id]->headers().getStatusValue());
  }

  std::string waitForUpstreamResponse(int request_id) {
    EXPECT_TRUE(responses_[request_id]->waitForEndStream());
    EXPECT_TRUE(responses_[request_id]->complete());
    return std::string(responses_[request_id]->headers().getStatusValue());
  }

  void sendRateLimitResponse(
      envoy::service::ratelimit::v3::RateLimitResponse::Code code,
      const Extensions::Filters::Common::RateLimit::DescriptorStatusList& descriptor_statuses,
      const Http::ResponseHeaderMap& response_headers_to_add,
      const Http::RequestHeaderMap& request_headers_to_add, int request_id) {
    ratelimit_requests_[request_id]->startGrpcStream();
    envoy::service::ratelimit::v3::RateLimitResponse response_msg;
    response_msg.set_overall_code(code);
    *response_msg.mutable_statuses() = {descriptor_statuses.begin(), descriptor_statuses.end()};

    response_headers_to_add.iterate(
        [&response_msg](const Http::HeaderEntry& h) -> Http::HeaderMap::Iterate {
          auto header = response_msg.mutable_response_headers_to_add()->Add();
          header->set_key(std::string(h.key().getStringView()));
          header->set_value(std::string(h.value().getStringView()));
          return Http::HeaderMap::Iterate::Continue;
        });
    request_headers_to_add.iterate(
        [&response_msg](const Http::HeaderEntry& h) -> Http::HeaderMap::Iterate {
          auto header = response_msg.mutable_request_headers_to_add()->Add();
          header->set_key(std::string(h.key().getStringView()));
          header->set_value(std::string(h.value().getStringView()));
          return Http::HeaderMap::Iterate::Continue;
        });
    ratelimit_requests_[request_id]->sendGrpcMessage(response_msg);
    ratelimit_requests_[request_id]->finishGrpcStream(Grpc::Status::Ok);
  }

  void setNumRequests(int num_requests) { num_requests_ = num_requests; }

  void cleanup() {
    if (fake_ratelimit_connection_ != nullptr) {
      AssertionResult result = fake_ratelimit_connection_->close();
      RELEASE_ASSERT(result, result.message());
    }
    cleanupUpstreamAndDownstream();
  }

  void basicFlow() {
    initiateClientConnection();
    waitForRatelimitRequest();
    sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OK, {},
                          Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);
    waitForSuccessfulUpstreamResponse(0);
    cleanup();

    EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
    EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
    EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
  }

  FakeHttpConnectionPtr fake_ratelimit_connection_;
  std::vector<FakeStreamPtr> ratelimit_requests_;
  std::vector<FakeStreamPtr> upstream_requests_;
  std::vector<IntegrationStreamDecoderPtr> responses_;

  const uint64_t request_size_ = 1024;
  const uint64_t response_size_ = 512;
  int num_requests_{1};
  bool failure_mode_deny_ = false;
  envoy::extensions::filters::http::ratelimit::v3::RateLimit::XRateLimitHeadersRFCVersion
      enable_x_ratelimit_headers_ = envoy::extensions::filters::http::ratelimit::v3::RateLimit::OFF;
  bool disable_x_envoy_ratelimited_header_ = false;
  envoy::extensions::filters::http::ratelimit::v3::RateLimit proto_config_{};
  std::string base_filter_config_ = R"EOF(
    domain: some_domain
    timeout: 0.5s
    response_headers_to_add:
    - header:
        key: x-global-ratelimit-service
        value: rate_limit_service
  )EOF";
};

// Test that verifies failure mode cases.
class RatelimitFailureModeIntegrationTest : public RatelimitIntegrationTest {
public:
  RatelimitFailureModeIntegrationTest() { failure_mode_deny_ = true; }
};

// Test verifies that response headers provided by filter work.
class RatelimitFilterHeadersEnabledIntegrationTest : public RatelimitIntegrationTest {
public:
  RatelimitFilterHeadersEnabledIntegrationTest() {
    enable_x_ratelimit_headers_ =
        envoy::extensions::filters::http::ratelimit::v3::RateLimit::DRAFT_VERSION_03;
  }
};

// Test verifies that disabling X-Envoy-RateLimited response header works.
class RatelimitFilterEnvoyRatelimitedHeaderDisabledIntegrationTest
    : public RatelimitIntegrationTest {
public:
  RatelimitFilterEnvoyRatelimitedHeaderDisabledIntegrationTest() {
    disable_x_envoy_ratelimited_header_ = true;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, RatelimitIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, RatelimitFailureModeIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, RatelimitFilterHeadersEnabledIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);
INSTANTIATE_TEST_SUITE_P(IpVersionsClientType,
                         RatelimitFilterEnvoyRatelimitedHeaderDisabledIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(RatelimitIntegrationTest, Ok) { basicFlow(); }

TEST_P(RatelimitIntegrationTest, OkWithHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();
  Http::TestResponseHeaderMapImpl ratelimit_response_headers{{"x-ratelimit-limit", "1000"},
                                                             {"x-ratelimit-remaining", "500"}};
  Http::TestRequestHeaderMapImpl request_headers_to_add{{"x-ratelimit-done", "true"}};

  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OK, {},
                        ratelimit_response_headers, request_headers_to_add, 0);
  waitForSuccessfulUpstreamResponse(0);

  ratelimit_response_headers.iterate(
      [response = responses_[0].get()](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value(), response->headers().get(lower_key)[0]->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      });
  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimit) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);
  waitForFailedUpstreamResponse(429, 0);

  EXPECT_THAT(responses_[0].get()->headers(),
              Http::HeaderValueOf(Http::Headers::get().EnvoyRateLimited,
                                  Http::Headers::get().EnvoyRateLimitedValues.True));

  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimitWithHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();
  Http::TestResponseHeaderMapImpl ratelimit_response_headers{
      {"x-ratelimit-limit", "1000"}, {"x-ratelimit-remaining", "0"}, {"retry-after", "33"}};
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        ratelimit_response_headers, Http::TestRequestHeaderMapImpl{}, 0);
  waitForFailedUpstreamResponse(429, 0);

  ratelimit_response_headers.iterate(
      [response = responses_[0].get()](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
        Http::LowerCaseString lower_key{std::string(entry.key().getStringView())};
        EXPECT_EQ(entry.value(), response->headers().get(lower_key)[0]->value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      });

  EXPECT_THAT(responses_[0].get()->headers(),
              Http::HeaderValueOf(Http::Headers::get().EnvoyRateLimited,
                                  Http::Headers::get().EnvoyRateLimitedValues.True));

  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, Error) {
  initiateClientConnection();
  waitForRatelimitRequest();
  ratelimit_requests_[0]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "404"}}, true);
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse(0);
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
    test_server_->waitForCounterGe("cluster.ratelimit_cluster.upstream_rq_timeout", 1);
    test_server_->waitForCounterGe("cluster.ratelimit_cluster.upstream_rq_504", 1);
    EXPECT_EQ(1, test_server_->counter("cluster.ratelimit_cluster.upstream_rq_timeout")->value());
    EXPECT_EQ(1, test_server_->counter("cluster.ratelimit_cluster.upstream_rq_504")->value());
    break;
  case Grpc::ClientType::GoogleGrpc:
    EXPECT_EQ(1, test_server_->counter("grpc.ratelimit_cluster.streams_total")->value());
    break;
  default:
    PANIC("reached unexpected code");
  }

  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse(0);

  cleanup();
}

TEST_P(RatelimitIntegrationTest, ConnectImmediateDisconnect) {
  initiateClientConnection();
  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_ratelimit_connection_));
  ASSERT_TRUE(fake_ratelimit_connection_->close());
  ASSERT_TRUE(fake_ratelimit_connection_->waitForDisconnect());
  fake_ratelimit_connection_ = nullptr;
  // Rate limiter fails open
  waitForSuccessfulUpstreamResponse(0);
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
  waitForSuccessfulUpstreamResponse(0);
  cleanup();
}

TEST_P(RatelimitFailureModeIntegrationTest, ErrorWithFailureModeOff) {
  initiateClientConnection();
  waitForRatelimitRequest();
  ratelimit_requests_[0]->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  // Rate limiter fail closed
  waitForFailedUpstreamResponse(500, 0);
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.error")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.failure_mode_allowed"));
}

TEST_P(RatelimitFilterHeadersEnabledIntegrationTest, OkWithFilterHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();

  Extensions::Filters::Common::RateLimit::DescriptorStatusList descriptor_statuses{
      Envoy::RateLimit::buildDescriptorStatus(
          1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2, 3),
      Envoy::RateLimit::buildDescriptorStatus(
          4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5, 6)};
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OK, descriptor_statuses,
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);
  waitForSuccessfulUpstreamResponse(0);

  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit,
          "1, 1;w=60;name=\"first\", 4;w=3600;name=\"second\""));
  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining,
          "2"));
  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset,
          "3"));

  cleanup();

  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.over_limit"));
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitFilterHeadersEnabledIntegrationTest, OverLimitWithFilterHeaders) {
  initiateClientConnection();
  waitForRatelimitRequest();

  Extensions::Filters::Common::RateLimit::DescriptorStatusList descriptor_statuses{
      Envoy::RateLimit::buildDescriptorStatus(
          1, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::MINUTE, "first", 2, 3),
      Envoy::RateLimit::buildDescriptorStatus(
          4, envoy::service::ratelimit::v3::RateLimitResponse::RateLimit::HOUR, "second", 5, 6)};
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT,
                        descriptor_statuses, Http::TestResponseHeaderMapImpl{},
                        Http::TestRequestHeaderMapImpl{}, 0);
  waitForFailedUpstreamResponse(429, 0);

  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitLimit,
          "1, 1;w=60;name=\"first\", 4;w=3600;name=\"second\""));
  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitRemaining,
          "2"));
  EXPECT_THAT(
      responses_[0].get()->headers(),
      Http::HeaderValueOf(
          Extensions::HttpFilters::Common::RateLimit::XRateLimitHeaders::get().XRateLimitReset,
          "3"));

  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitFilterEnvoyRatelimitedHeaderDisabledIntegrationTest,
       OverLimitWithoutEnvoyRatelimitedHeader) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);
  waitForFailedUpstreamResponse(429, 0);

  EXPECT_THAT(responses_[0].get()->headers(),
              ::testing::Not(Http::HeaderValueOf(Http::Headers::get().EnvoyRateLimited, _)));

  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimitAndOK) {
  const int num_requests = 4;
  setNumRequests(num_requests);

  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OK, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);

  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OK, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 1);
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 2);

  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 3);
  std::map<std::string, int> status_cnt;
  for (int i = 0; i < num_requests; i++) {
    status_cnt[waitForUpstreamResponse(i)]++;
  }
  EXPECT_EQ(status_cnt["200"], 2);
  EXPECT_EQ(status_cnt["429"], 2);

  cleanup();

  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.ratelimit.ok")->value());
  EXPECT_EQ(2, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

TEST_P(RatelimitIntegrationTest, OverLimitResponseHeadersToAdd) {
  initiateClientConnection();
  waitForRatelimitRequest();
  sendRateLimitResponse(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT, {},
                        Http::TestResponseHeaderMapImpl{}, Http::TestRequestHeaderMapImpl{}, 0);
  waitForFailedUpstreamResponse(429, 0);

  EXPECT_THAT(responses_[0].get()->headers(),
              Http::HeaderValueOf(Http::Headers::get().EnvoyRateLimited,
                                  Http::Headers::get().EnvoyRateLimitedValues.True));
  EXPECT_THAT(responses_[0].get()->headers(),
              Http::HeaderValueOf("x-global-ratelimit-service", "rate_limit_service"));
  cleanup();

  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.ok"));
  EXPECT_EQ(1, test_server_->counter("cluster.cluster_0.ratelimit.over_limit")->value());
  EXPECT_EQ(nullptr, test_server_->counter("cluster.cluster_0.ratelimit.error"));
}

} // namespace
} // namespace Envoy
