#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/rate_limit_quota/filter.h"

#include "test/common/http/common.h"
#include "test/extensions/filters/http/rate_limit_quota/test_utils.h"
#include "test/integration/http_integration.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

enum class BlanketRule {
  NOT_SPECIFIED,
  ALLOW_ALL,
  DENY_ALL,
};

struct ConfigOption {
  bool valid_rlqs_server = true;
  BlanketRule no_assignment_blanket_rule = BlanketRule::NOT_SPECIFIED;
  bool unsupported_no_assignment_strategy = false;
  BlanketRule expired_assignment_blanket_rule = BlanketRule::NOT_SPECIFIED;
};

// These tests exercise the rate limit quota filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class RateLimitQuotaIntegrationTest
    : public Event::TestUsingSimulatedTime,
      public HttpIntegrationTest,
      public Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing {
protected:
  RateLimitQuotaIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();

    // Create separate side stream for rate limit quota server
    for (int i = 0; i < 2; ++i) {
      grpc_upstreams_.push_back(&addFakeUpstream(Http::CodecType::HTTP2));
    }
  }

  void initializeConfig(ConfigOption config_option = {}, const std::string& log_format = "") {
    config_helper_.addConfigModifier([this, config_option, log_format](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for headers
      ConfigHelper::setHttp2(
          *(bootstrap.mutable_static_resources()->mutable_clusters()->Mutable(0)));

      // Enable access logging for testing dynamic metadata.
      if (!log_format.empty()) {
        HttpIntegrationTest::useAccessLog(log_format);
      }

      // Clusters for ExtProc gRPC servers, starting by copying an existing cluster
      for (size_t i = 0; i < grpc_upstreams_.size(); ++i) {
        auto* server_cluster = bootstrap.mutable_static_resources()->add_clusters();
        server_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
        std::string cluster_name = absl::StrCat("rlqs_server_", i);
        server_cluster->set_name(cluster_name);
        server_cluster->mutable_load_assignment()->set_cluster_name(cluster_name);
      }

      if (config_option.valid_rlqs_server) {
        // Load configuration of the server from YAML and use a helper to add a grpc_service
        // stanza pointing to the cluster that we just made
        setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_server_0",
                       grpc_upstreams_[0]->localAddress());
      } else {
        // Set up the gRPC service with wrong cluster name and address.
        setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_wrong_server",
                       std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
      }

      // Set the domain name.
      proto_config_.set_domain("cloud_12345_67890_rlqs");

      xds::type::matcher::v3::Matcher matcher;
      TestUtility::loadFromYaml(std::string(ValidMatcherConfig), matcher);

      auto* mutable_config = matcher.mutable_matcher_list()
                                 ->mutable_matchers(0)
                                 ->mutable_on_match()
                                 ->mutable_action()
                                 ->mutable_typed_config();
      ASSERT_TRUE(mutable_config->Is<::envoy::extensions::filters::http::rate_limit_quota::v3::
                                         RateLimitQuotaBucketSettings>());

      auto mutable_bucket_settings = MessageUtil::anyConvert<
          ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings>(
          *mutable_config);
      // Configure the no_assignment behavior.
      if (config_option.no_assignment_blanket_rule != BlanketRule::NOT_SPECIFIED) {
        if (config_option.no_assignment_blanket_rule == BlanketRule::ALLOW_ALL) {
          mutable_bucket_settings.mutable_no_assignment_behavior()
              ->mutable_fallback_rate_limit()
              ->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
        } else if (config_option.no_assignment_blanket_rule == BlanketRule::DENY_ALL) {
          mutable_bucket_settings.mutable_no_assignment_behavior()
              ->mutable_fallback_rate_limit()
              ->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }
      } else if (config_option.unsupported_no_assignment_strategy) {
        auto* requests_per_time_unit = mutable_bucket_settings.mutable_no_assignment_behavior()
                                           ->mutable_fallback_rate_limit()
                                           ->mutable_requests_per_time_unit();
        requests_per_time_unit->set_requests_per_time_unit(100);
        requests_per_time_unit->set_time_unit(envoy::type::v3::RateLimitUnit::SECOND);
      }

      if (config_option.expired_assignment_blanket_rule != BlanketRule::NOT_SPECIFIED) {
        if (config_option.expired_assignment_blanket_rule == BlanketRule::ALLOW_ALL) {
          mutable_bucket_settings.mutable_expired_assignment_behavior()
              ->mutable_fallback_rate_limit()
              ->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
        } else if (config_option.expired_assignment_blanket_rule == BlanketRule::DENY_ALL) {
          mutable_bucket_settings.mutable_expired_assignment_behavior()
              ->mutable_fallback_rate_limit()
              ->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }
        mutable_bucket_settings.mutable_expired_assignment_behavior()
            ->mutable_expired_assignment_behavior_timeout()
            ->set_seconds(15);
      }

      mutable_config->PackFrom(mutable_bucket_settings);
      proto_config_.mutable_bucket_matchers()->MergeFrom(matcher);

      // Construct a configuration proto for our filter and then re-write it
      // to JSON so that we can add it to the overall config
      envoy::config::listener::v3::Filter rate_limit_quota_filter;
      rate_limit_quota_filter.set_name("envoy.filters.http.rate_limit_quota");
      rate_limit_quota_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(
          MessageUtil::getJsonStringFromMessageOrError(rate_limit_quota_filter));

      // Parameterize with defer processing to prevent bit rot as filter made
      // assumptions of data flow, prior relying on eager processing.
      config_helper_.addRuntimeOverride(Runtime::defer_processing_backedup_streams,
                                        deferredProcessing() ? "true" : "false");
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  // Send downstream client request.
  void
  sendClientRequest(const absl::flat_hash_map<std::string, std::string>* custom_headers = nullptr) {
    auto conn = makeClientConnection(lookupPort("http"));
    codec_client_ = makeHttpConnection(std::move(conn));
    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (custom_headers != nullptr) {
      for (auto const& pair : *custom_headers) {
        headers.addCopy(pair.first, pair.second);
      }
    }
    response_ = codec_client_->makeHeaderOnlyRequest(headers);
  }

  void cleanUp() {
    if (rlqs_connection_) {
      ASSERT_TRUE(rlqs_connection_->close());
      ASSERT_TRUE(rlqs_connection_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void TearDown() override { cleanUp(); }

  bool expectAllowedRequest() {
    if (!fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_))
      return false;
    if (!fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_))
      return false;
    if (!upstream_request_->waitForEndStream(*dispatcher_))
      return false;
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    if (!response_->waitForEndStream())
      return false;
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Clean up the upstream and downstream resource but keep the gRPC
    // connection to RLQS server open.
    cleanupUpstreamAndDownstream();
    return true;
  }

  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig
      proto_config_{};
  std::vector<FakeUpstream*> grpc_upstreams_;
  FakeHttpConnectionPtr rlqs_connection_;
  FakeStreamPtr rlqs_stream_;
  IntegrationStreamDecoderPtr response_;
  int report_interval_sec = 60;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeDeferredProcessing, RateLimitQuotaIntegrationTest,
    GRPC_CLIENT_INTEGRATION_DEFERRED_PROCESSING_PARAMS,
    Grpc::GrpcClientIntegrationParamTestWithDeferredProcessing::protocolTestParamsToString);

TEST_P(RateLimitQuotaIntegrationTest, StarFailed) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
  ConfigOption option;
  option.valid_rlqs_server = false;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  sendClientRequest(&custom_headers);
  EXPECT_FALSE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_,
                                                         std::chrono::milliseconds(25000)));
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowEmptyResponse) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Send the response from RLQS server.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  // Response with empty bucket action.
  rlqs_response.add_bucket_action();
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowResponseNotMatched) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Build the response whose bucket ID doesn't match the sent report.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  (*bucket_action->mutable_bucket_id()->mutable_bucket())
      .insert({"not_matched_key", "not_matched_value"});
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowResponseMatched) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Build the response whose bucket ID matches the sent report.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  custom_headers.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");
}

TEST_P(RateLimitQuotaIntegrationTest, TestBasicMetadataLogging) {
  initializeConfig({}, "Whole Bucket "
                       "ID=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_"
                       "limit_quota.bucket)%\n"
                       "Name=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_"
                       "limit_quota.bucket:name)%");
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Build the response whose bucket ID matches the sent report.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  custom_headers.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");

  std::string log_output0 =
      HttpIntegrationTest::waitForAccessLog(HttpIntegrationTest::access_log_name_, 0, true);
  EXPECT_THAT(log_output0, testing::HasSubstr("Whole Bucket ID"));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"name\":\"prod\""));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"group\":\"envoy\""));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"environment\":\"staging\""));
  std::string log_output1 =
      HttpIntegrationTest::waitForAccessLog(HttpIntegrationTest::access_log_name_, 1, true);
  EXPECT_THAT(log_output1, testing::HasSubstr("Name=prod"));
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowMultiSameRequest) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  for (int i = 0; i < 3; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Second downstream client request will not trigger the reports to RLQS server since it is
    // same as first request, which will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();

      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
      }

      // Send the response from RLQS server.
      rlqs_stream_->sendGrpcMessage(rlqs_response);
    }
    // Handle the request received by upstream.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowMultiDifferentRequest) {
  initializeConfig();
  HttpIntegrationTest::initialize();

  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers = {
      {{"environment", "staging"}, {"group", "envoy"}},
      {{"environment", "staging"}, {"group", "envoy_1"}},
      {{"environment", "staging"}, {"group", "envoy_2"}}};
  int header_size = custom_headers.size();
  for (int i = 0; i < header_size; ++i) {
    // Send downstream client request to upstream.
    if (i == 0) {
      sendClientRequest(&custom_headers[i]);
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();
    } else {
      sendClientRequest(&custom_headers[i]);

      // No need to start gRPC stream again since it is kept open.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
    }

    // Build the response.
    envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
    absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers[i];
    custom_headers_cpy.insert({"name", "prod"});
    auto* bucket_action = rlqs_response.add_bucket_action();
    for (const auto& [key, value] : custom_headers_cpy) {
      (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
    }

    // Send the response from RLQS server.
    rlqs_stream_->sendGrpcMessage(rlqs_response);

    // Handle the request received by upstream.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Clean up the upstream and downstream resource but keep the gRPC connection to RLQS server
    // open.
    cleanupUpstreamAndDownstream();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestNoAssignmentDenyAll) {
  ConfigOption option;
  option.no_assignment_blanket_rule = BlanketRule::DENY_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  for (int i = 0; i < 3; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // After first request, the downstream client request will not trigger the reports to RLQS
    // server since the request which is same as first request will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The request is denied by no_assignment_behavior.
      EXPECT_EQ(usage.num_requests_allowed(), 0);
      EXPECT_EQ(usage.num_requests_denied(), 1);
    }

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    // Verify that all the denied requests send the 429 local reply.
    EXPECT_EQ(response_->headers().getStatusValue(), "429");

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiDifferentRequestNoAssignementAllowAll) {
  ConfigOption option;
  option.no_assignment_blanket_rule = BlanketRule::ALLOW_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();

  int totol_num_of_request = 10;
  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers(
      totol_num_of_request, {{"environment", "staging"}});
  std::string key_prefix = "envoy_";
  for (int i = 0; i < totol_num_of_request; ++i) {
    custom_headers[i].insert({"group", absl::StrCat(key_prefix, i)});
  }

  for (int i = 0; i < totol_num_of_request; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers[i]);
    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The first request is allowed due to ALLOW_ALL no_assignment_behavior
      EXPECT_EQ(usage.num_requests_allowed(), 1);
      EXPECT_EQ(usage.num_requests_denied(), 0);
    } else {
      // No need to start gRPC stream again since it is kept open.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

      // Verify usage content.
      for (const auto& usage : reports.bucket_quota_usages()) {
        // Only the client request that triggers usage report is allowed.
        // It is because the usage report only account for last reporting period.
        if (usage.bucket_id().bucket().at("group") == absl::StrCat(key_prefix, i)) {
          EXPECT_EQ(usage.num_requests_allowed(), 1);
          EXPECT_EQ(usage.num_requests_denied(), 0);
        } else {
          EXPECT_EQ(usage.num_requests_allowed(), 0);
          EXPECT_EQ(usage.num_requests_denied(), 0);
        }
      }
    }

    // No RLQS server response is sent back in this test.

    // Handle the request received by upstream.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Clean up the upstream and downstream resource but keep the gRPC connection to RLQS server
    // open.
    cleanupUpstreamAndDownstream();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiDifferentRequestNoAssignementDenyAll) {
  ConfigOption option;
  option.no_assignment_blanket_rule = BlanketRule::DENY_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();

  int totol_num_of_request = 5;
  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers(
      totol_num_of_request, {{"environment", "staging"}});
  std::string key_prefix = "envoy_";
  for (int i = 0; i < totol_num_of_request; ++i) {
    custom_headers[i].insert({"group", absl::StrCat(key_prefix, i)});
  }

  for (int i = 0; i < totol_num_of_request; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers[i]);
    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The request is denied by no_assignment_behavior.
      EXPECT_EQ(usage.num_requests_allowed(), 0);
      EXPECT_EQ(usage.num_requests_denied(), 1);
    } else {
      // No need to start gRPC stream again since it is kept open.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
    }
    // No RLQS server response is sent back in this test.

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    // Verify that all the denied requests send the 429 local reply.
    EXPECT_EQ(response_->headers().getStatusValue(), "429");

    // Clean up the upstream and downstream resource but keep the gRPC connection to RLQS server
    // open.
    cleanupUpstreamAndDownstream();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowPeriodicalReport) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  // reports should be built in filter.cc
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Verify the usage report content.
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // We only send single downstream client request and it is allowed.
  EXPECT_EQ(usage.num_requests_allowed(), 1);
  EXPECT_EQ(usage.num_requests_denied(), 0);
  // It is first report so the time_elapsed is 0.
  EXPECT_EQ(Protobuf::util::TimeUtil::DurationToSeconds(usage.time_elapsed()), 0);

  rlqs_stream_->startGrpcStream();

  // Build the response.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();

  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");

  // Trigger the report periodically, 10 times.
  for (int i = 0; i < 10; ++i) {
    // Advance the time by report_interval.
    simTime().advanceTimeWait(std::chrono::milliseconds(report_interval_sec * 1000));
    // Checks that the rate limit server has received the periodical reports.
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

    // Verify the usage report content.
    ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
    const auto& usage = reports.bucket_quota_usages(0);
    // Report only represents the usage since last report.
    // In the periodical report case here, the number of request allowed and denied is 0 since no
    // new requests comes in.
    EXPECT_EQ(usage.num_requests_allowed(), 0);
    EXPECT_EQ(usage.num_requests_denied(), 0);
    // time_elapsed equals to periodical reporting interval.
    EXPECT_EQ(Protobuf::util::TimeUtil::DurationToSeconds(usage.time_elapsed()),
              report_interval_sec);

    // Build the rlqs server response.
    envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response2;
    auto* bucket_action2 = rlqs_response2.add_bucket_action();

    for (const auto& [key, value] : custom_headers_cpy) {
      (*bucket_action2->mutable_bucket_id()->mutable_bucket()).insert({key, value});
    }
    rlqs_stream_->sendGrpcMessage(rlqs_response2);
  }
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowPeriodicalReportWithStreamClosed) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  // reports should be built in filter.cc
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Verify the usage report content.
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // We only send single downstream client request and it is allowed.
  EXPECT_EQ(usage.num_requests_allowed(), 1);
  EXPECT_EQ(usage.num_requests_denied(), 0);
  // It is first report so the time_elapsed is 0.
  EXPECT_EQ(Protobuf::util::TimeUtil::DurationToSeconds(usage.time_elapsed()), 0);

  rlqs_stream_->startGrpcStream();

  // Build the response.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();

  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");

  // ValidMatcherConfig.
  int report_interval_sec = 60;
  // Trigger the report periodically.
  for (int i = 0; i < 6; ++i) {
    if (i == 2) {
      // Close the stream.
      WAIT_FOR_LOG_CONTAINS("debug", "gRPC stream closed remotely with status",
                            { rlqs_stream_->finishGrpcStream(Grpc::Status::Canceled); });
      ASSERT_TRUE(rlqs_stream_->waitForReset());
    }

    // Advance the time by report_interval.
    simTime().advanceTimeWait(std::chrono::milliseconds(report_interval_sec * 1000));

    if (i == 2) {
      // Stream should be restarted when next required for usage reporting.
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();
    }

    // Only perform rlqs server check and response before stream is remotely
    // closed. Checks that the rate limit server has received the periodical
    // reports.
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

    // Verify the usage report content.
    ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
    const auto& usage = reports.bucket_quota_usages(0);
    // Report only represents the usage since last report.
    // In the periodical report case here, the number of request allowed and
    // denied is 0 since no new requests comes in.
    EXPECT_EQ(usage.num_requests_allowed(), 0);
    EXPECT_EQ(usage.num_requests_denied(), 0);
    // time_elapsed equals to periodical reporting interval.
    EXPECT_EQ(Protobuf::util::TimeUtil::DurationToSeconds(usage.time_elapsed()),
              report_interval_sec);

    // Build the rlqs server response.
    envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response2;
    auto* bucket_action2 = rlqs_response2.add_bucket_action();

    for (const auto& [key, value] : custom_headers_cpy) {
      (*bucket_action2->mutable_bucket_id()->mutable_bucket()).insert({key, value});
    }
    rlqs_stream_->sendGrpcMessage(rlqs_response2);
  }
}

// RLQS filter is operating in non-blocking mode now, this test could be flaky until the stats are
// added to make the test behavior deterministic. (e.g., wait for stats in the test).
// Disable the test for now.
TEST_P(RateLimitQuotaIntegrationTest, DISABLED_MultiRequestWithTokenBucketThrottling) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int max_token = 1;
  int tokens_per_fill = 30;
  int fill_interval_sec = 60;
  int fill_one_token_in_ms = fill_interval_sec / tokens_per_fill * 1000;
  // First  request: allowed; fail-open, default no assignment policy.
  // Second request: allowed; one token remaining, token bucket's max_token is 1.
  // Third  request: allowed; token bucket has been refilled by advancing 2s.
  // Fourth request: rejected; no token left and no token refilled.
  // Fifth  request: allowed; token bucket has been refilled by advancing 2s.
  // Sixth  request: rejected; no token left and no token refilled.
  for (int i = 0; i < 6; ++i) {
    // We advance time by 2s for 3rd and 5th requests so that token bucket can
    // be refilled.
    if (i == 2 || i == 4) {
      simTime().advanceTimeAndRun(std::chrono::milliseconds(fill_one_token_in_ms), *dispatcher_,
                                  Envoy::Event::Dispatcher::RunType::NonBlock);
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        auto* token_bucket = strategy->mutable_token_bucket();
        token_bucket->set_max_tokens(max_token);
        token_bucket->mutable_tokens_per_fill()->set_value(30);
        token_bucket->mutable_fill_interval()->set_seconds(60);
      }

      // Send the response from RLQS server.
      rlqs_stream_->sendGrpcMessage(rlqs_response);
    }

    // 4th and 6th request are throttled.
    if (i == 3 || i == 5) {
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "429");
    } else {
      // Handle the request received by upstream.
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
      upstream_request_->encodeData(100, true);

      // Verify the response to downstream.
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "200");
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithTokenBucket) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int max_token = 1;
  int tokens_per_fill = 30;
  int fill_interval_sec = 60;
  int fill_one_token_in_ms = fill_interval_sec / tokens_per_fill * 1000;
  for (int i = 0; i < 10; ++i) {
    // We advance time by 2s so that token bucket can be refilled.
    if (i == 4 || i == 6) {
      simTime().advanceTimeAndRun(std::chrono::milliseconds(fill_one_token_in_ms), *dispatcher_,
                                  Envoy::Event::Dispatcher::RunType::NonBlock);
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        auto* token_bucket = strategy->mutable_token_bucket();
        token_bucket->set_max_tokens(max_token);
        token_bucket->mutable_tokens_per_fill()->set_value(30);
        token_bucket->mutable_fill_interval()->set_seconds(60);
      }

      // Send the response from RLQS server.
      rlqs_stream_->sendGrpcMessage(rlqs_response);
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsupportedStrategy) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 2; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Handle the request received by upstream. All requests will be allowed
    // since the strategy is not supported.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        auto* unsupported_strategy = strategy->mutable_requests_per_time_unit();
        unsupported_strategy->set_requests_per_time_unit(10);
        unsupported_strategy->set_time_unit(envoy::type::v3::RateLimitUnit::SECOND);
      }

      // Send the response from RLQS server.
      rlqs_stream_->sendGrpcMessage(rlqs_response);
      absl::SleepFor(absl::Seconds(1));
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsetStrategy) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 2; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Handle the request received by upstream. All requests will be allowed
    // since the strategy is not set.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
      }

      // Send the response from RLQS server.
      rlqs_stream_->sendGrpcMessage(rlqs_response);
      absl::SleepFor(absl::Seconds(1));
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsupportedDefaultAction) {
  ConfigOption option;
  option.unsupported_no_assignment_strategy = true;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Handle the request received by upstream. All requests will be allowed
  // since the strategy is not set.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
  upstream_request_->encodeData(100, true);

  // Verify the response to downstream.
  ASSERT_TRUE(response_->waitForEndStream());
  EXPECT_TRUE(response_->complete());
  EXPECT_EQ(response_->headers().getStatusValue(), "200");

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  cleanUp();
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpiredAssignmentDeny) {
  ConfigOption option;
  option.expired_assignment_blanket_rule = BlanketRule::DENY_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  for (int i = 0; i < 2; ++i) {
    // Advance the time to make cached assignment expired.
    if (i == 1) {
      simTime().advanceTimeWait(std::chrono::milliseconds(12 * 1000));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // 2nd downstream client request will not trigger the reports to RLQS server since it is
    // same as first request, which will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();

      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(10);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
      }

      // Send the response from RLQS server and wait for response processing to
      // finish for test consistency.
      WAIT_FOR_LOG_CONTAINS("debug", "Assignment cached for bucket id",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }

    // 2nd request is throttled because the assignment has expired and
    // expired assignment behavior is DENY_ALL.
    if (i == 1) {
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "429");
    } else {
      // Handle the request received by upstream.
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
      upstream_request_->encodeData(100, true);

      // Verify the response to downstream.
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "200");
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpiredAssignmentAllow) {
  ConfigOption option;
  option.expired_assignment_blanket_rule = BlanketRule::ALLOW_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  for (int i = 0; i < 3; ++i) {
    // Advance the time to make cached assignment expired.
    if (i == 1) {
      simTime().advanceTimeWait(std::chrono::milliseconds(3 * 1000));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // 2nd downstream client request will not trigger the reports to RLQS server
    // since it is same as first request, which will find the entry in the
    // cache.
    if (i != 1) {
      // 1st request will start the gRPC stream.
      if (i == 0) {
        // Start the gRPC stream to RLQS server on the first request.
        ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();
      } else {
        // 3rd request won't start gRPC stream again since it is kept open and
        // the usage will be aggregated instead of spawning an immediate report.
        envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
        ASSERT_FALSE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      }

      // Build the response.
      envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();

      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(2);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
      }

      // Send the response from RLQS server and wait for response processing to
      // finish for test consistency.
      WAIT_FOR_LOG_CONTAINS("debug", "Assignment cached for bucket id",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }

    // Even though assignment was expired on 2nd request, the request is still
    // allowed because the expired assignment behavior is ALLOW_ALL.
    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
    upstream_request_->encodeData(100, true);

    // Verify the response to downstream.
    ASSERT_TRUE(response_->waitForEndStream());
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), "200");

    // Clean up the upstream and downstream resource but keep the gRPC
    // connection to RLQS server open.
    cleanupUpstreamAndDownstream();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpirationToDefaultDeny) {
  ConfigOption option;
  option.expired_assignment_blanket_rule = BlanketRule::DENY_ALL;
  option.no_assignment_blanket_rule = BlanketRule::ALLOW_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 4; ++i) {
    // Advance the time to make cached assignment expired.
    if (i > 0) {
      simTime().advanceTimeWait(std::chrono::seconds(15));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Query 1: ALLOW_ALL by default. Query 2: DENY_ALL by assignment.
    // Query 3: DENY_ALL by assignment expiration. Query 4: ALLOW_ALL by default.
    if (i == 0 || i == 3) {
      // Handle the request received by upstream.
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
      upstream_request_->encodeData(100, true);

      // Verify the response to downstream.
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "200");

      if (i == 0) {
        // Start the gRPC stream to RLQS server & send the initial report.
        ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();

        // Build the response.
        envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
        absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
        custom_headers_cpy.insert({"name", "prod"});
        auto* bucket_action = rlqs_response.add_bucket_action();

        for (const auto& [key, value] : custom_headers_cpy) {
          (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
          auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
          quota_assignment->mutable_assignment_time_to_live()->set_seconds(15);
          auto* strategy = quota_assignment->mutable_rate_limit_strategy();
          strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }

        // Send the response from RLQS server.
        rlqs_stream_->sendGrpcMessage(rlqs_response);
        absl::SleepFor(absl::Seconds(1));
      }
    } else {
      ASSERT_TRUE(response_->waitForEndStream(std::chrono::seconds(5)));
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "429");
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpirationWithoutFallback) {
  ConfigOption option;
  option.no_assignment_blanket_rule = BlanketRule::ALLOW_ALL;
  initializeConfig(option);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 3; ++i) {
    // Advance the time to make cached assignment expired.
    if (i > 0) {
      simTime().advanceTimeWait(std::chrono::seconds(15));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Query 1: ALLOW_ALL by default. Query 2: DENY_ALL by assignment.
    // Query 3: DENY_ALL by assignment expiration. Query 4: ALLOW_ALL by default.
    if (i == 0 || i == 2) {
      // Handle the request received by upstream.
      ASSERT_TRUE(
          fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
      ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
      ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));
      upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, false);
      upstream_request_->encodeData(100, true);

      // Verify the response to downstream.
      ASSERT_TRUE(response_->waitForEndStream());
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "200");

      if (i == 0) {
        // Start the gRPC stream to RLQS server & send the initial report.
        ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();

        // Build the response.
        envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
        absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
        custom_headers_cpy.insert({"name", "prod"});
        auto* bucket_action = rlqs_response.add_bucket_action();

        for (const auto& [key, value] : custom_headers_cpy) {
          (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
          auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
          quota_assignment->mutable_assignment_time_to_live()->set_seconds(15);
          auto* strategy = quota_assignment->mutable_rate_limit_strategy();
          strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }

        // Send the response from RLQS server.
        rlqs_stream_->sendGrpcMessage(rlqs_response);
        absl::SleepFor(absl::Seconds(1));
      }
    } else {
      ASSERT_TRUE(response_->waitForEndStream(std::chrono::seconds(5)));
      EXPECT_TRUE(response_->complete());
      EXPECT_EQ(response_->headers().getStatusValue(), "429");
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithAbandonAction) {
  initializeConfig();
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});

  // Send first request & expect a new RLQS stream.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(grpc_upstreams_[0]->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Expect an initial report.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();
  EXPECT_EQ(reports.bucket_quota_usages_size(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_allowed(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_denied(), 0);

  // Expect the first request to be allowed.
  ASSERT_TRUE(expectAllowedRequest());

  // Build an abandon-action response.
  envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  bucket_action->mutable_abandon_action();
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Expect the next report to be empty, since the cache entry was removed, but
  // allow for retries in case the response hasn't processed yet.
  bool empty_report = false;
  for (int i = 0; i < 5 && !empty_report; ++i) {
    simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec));
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
    empty_report = reports.bucket_quota_usages().empty();
  }
  ASSERT_TRUE(empty_report);

  // Send a second request & expect an immediate report for the new bucket, but
  // no new stream.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  EXPECT_EQ(reports.bucket_quota_usages_size(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_allowed(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_denied(), 0);

  // Expect the second request to be allowed.
  ASSERT_TRUE(expectAllowedRequest());
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
