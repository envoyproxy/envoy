#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/http/codec.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/type/v3/ratelimit_strategy.pb.h"

#include "source/common/common/assert.h"
#include "source/common/network/address_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/rate_limit_quota/filter_persistence.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/http/common.h"
#include "test/config/utility.h"
#include "test/extensions/filters/http/rate_limit_quota/test_utils.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration_stream_decoder.h"
#include "test/test_common/environment.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Envoy::ProtoEq;
using envoy::config::cluster::v3::Cluster;
using envoy::service::rate_limit_quota::v3::BucketId;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using Protobuf::util::MessageDifferencer;
using ::xds::type::matcher::v3::Matcher;
using ValueBuilder = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings::BucketIdBuilder::ValueBuilder;
using MatcherList = Matcher::MatcherList;
using FieldMatcher = MatcherList::FieldMatcher;
using OnMatch = Matcher::OnMatch;

MATCHER_P2(ProtoEqIgnoringFieldAndOrdering, expected,
           /* const FieldDescriptor* */ ignored_field, "") {
  MessageDifferencer differencer;
  ASSERT(ignored_field != nullptr, "Field to ignore not found.");
  differencer.IgnoreField(ignored_field);
  differencer.set_repeated_field_comparison(MessageDifferencer::AS_SET);

  if (differencer.Compare(arg, expected)) {
    return true;
  }
  *result_listener << "Expected:\n" << expected.DebugString() << "\nActual:\n" << arg.DebugString();
  return false;
}

using BlanketRule = envoy::type::v3::RateLimitStrategy::BlanketRule;
using envoy::type::v3::RateLimitStrategy;
using DenyResponseSettings = envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings::DenyResponseSettings;

static const int kFallbackTtlSecDefault = 15;

// These tests exercise the rate limit quota filter through Envoy's integration test
// environment by configuring an instance of the Envoy server and driving it
// through the mock network stack.
class RateLimitQuotaIntegrationTest : public Event::TestUsingSimulatedTime,
                                      public HttpIntegrationTest,
                                      public Grpc::GrpcClientIntegrationParamTest {
protected:
  RateLimitQuotaIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()),
        default_matcher_(constructMatcher()) {
    deny_all_strategy.set_blanket_rule(RateLimitStrategy::DENY_ALL);
    allow_all_strategy.set_blanket_rule(RateLimitStrategy::ALLOW_ALL);
  }

  void createUpstreams() override {
    setUpstreamCount(2);

    autonomous_upstream_ = true;
    traffic_endpoint_ = upstream_address_fn_(0);
    createUpstream(traffic_endpoint_, upstreamConfig());
    traffic_upstream_ = dynamic_cast<AutonomousUpstream*>(fake_upstreams_[0].get());

    autonomous_upstream_ = false;
    // Testing requires multiple RLQS upstream targets.
    rlqs_endpoint_ = upstream_address_fn_(1);
    createUpstream(rlqs_endpoint_, upstreamConfig());
    rlqs_upstream_ = fake_upstreams_[1].get();
  }

  // Changes to apply to a matcher's OnMatch and its underlying RateLimitQuotaBucketSettings.
  struct Manipulations {
    absl::optional<BlanketRule> no_assignment_blanket_rule = std::nullopt;
    bool unsupported_no_assignment_strategy = false;
    absl::optional<BucketId> custom_bucket_id = std::nullopt;
    absl::optional<RateLimitStrategy> fallback_rate_limit_strategy = std::nullopt;
    int fallback_ttl_sec = kFallbackTtlSecDefault;
    absl::optional<DenyResponseSettings> deny_response_settings = std::nullopt;
  };

  void manipulateOnMatch(const Manipulations& config_option, OnMatch* mutable_on_match) {
    auto* mutable_config = mutable_on_match->mutable_action()->mutable_typed_config();

    ASSERT_TRUE(mutable_config->Is<::envoy::extensions::filters::http::rate_limit_quota::v3::
                                       RateLimitQuotaBucketSettings>());

    auto mutable_bucket_settings = MessageUtil::anyConvert<
        ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings>(
        *mutable_config);

    if (config_option.custom_bucket_id.has_value()) {
      auto* bucket_id_builder =
          mutable_bucket_settings.mutable_bucket_id_builder()->mutable_bucket_id_builder();
      bucket_id_builder->clear();
      for (const auto& [key, value] : config_option.custom_bucket_id->bucket()) {
        ValueBuilder value_builder;
        value_builder.set_string_value(value);
        bucket_id_builder->insert({key, value_builder});
      }
    }

    // Configure the no_assignment behavior.
    if (config_option.no_assignment_blanket_rule.has_value()) {
      mutable_bucket_settings.mutable_no_assignment_behavior()
          ->mutable_fallback_rate_limit()
          ->set_blanket_rule(*config_option.no_assignment_blanket_rule);
    } else if (config_option.unsupported_no_assignment_strategy) {
      auto* requests_per_time_unit = mutable_bucket_settings.mutable_no_assignment_behavior()
                                         ->mutable_fallback_rate_limit()
                                         ->mutable_requests_per_time_unit();
      requests_per_time_unit->set_requests_per_time_unit(100);
      requests_per_time_unit->set_time_unit(envoy::type::v3::RateLimitUnit::SECOND);
    }

    if (config_option.fallback_rate_limit_strategy.has_value()) {
      *mutable_bucket_settings.mutable_expired_assignment_behavior()
           ->mutable_fallback_rate_limit() = *config_option.fallback_rate_limit_strategy;
      mutable_bucket_settings.mutable_expired_assignment_behavior()
          ->mutable_expired_assignment_behavior_timeout()
          ->set_seconds(config_option.fallback_ttl_sec);
    }

    if (config_option.deny_response_settings.has_value()) {
      *mutable_bucket_settings.mutable_deny_response_settings() =
          *config_option.deny_response_settings;
    }

    mutable_config->PackFrom(mutable_bucket_settings);
  }

  static Matcher constructPreviewMatcher() {
    Matcher matcher_out;
    TestUtility::loadFromYaml(std::string(ValidPreviewMatcherConfig), matcher_out);
    return matcher_out;
  }

  static Matcher constructMatcher() {
    Matcher matcher_out;
    TestUtility::loadFromYaml(std::string(ValidMatcherConfig), matcher_out);
    return matcher_out;
  }

  void initializeConfig(const Matcher& matcher, bool valid_rlqs_server = true,
                        const std::string& log_format = "") {
    config_helper_.addConfigModifier([this, &matcher, valid_rlqs_server, log_format](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Enable access logging for testing dynamic metadata.
      if (!log_format.empty()) {
        HttpIntegrationTest::useAccessLog(log_format);
      }

      traffic_cluster_ = bootstrap.mutable_static_resources()->mutable_clusters(0);
      // Ensure "HTTP2 with no prior knowledge." Necessary for gRPC and for
      // headers
      ConfigHelper::setHttp2(*traffic_cluster_);
      traffic_cluster_->set_name("cluster_0");
      traffic_cluster_->mutable_load_assignment()->set_cluster_name("cluster_0");

      rlqs_cluster_ = bootstrap.mutable_static_resources()->add_clusters();
      rlqs_cluster_->MergeFrom(bootstrap.static_resources().clusters(0));
      rlqs_cluster_->set_name("rlqs_server_0");
      rlqs_cluster_->mutable_load_assignment()->set_cluster_name("rlqs_server_0");

      if (valid_rlqs_server) {
        // Load configuration of the server from YAML and use a helper to
        // add a grpc_service stanza pointing to the cluster that we just
        // made
        setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_server_0",
                       rlqs_upstream_->localAddress());
      } else {
        // Set up the gRPC service with wrong cluster name and address.
        setGrpcService(*proto_config_.mutable_rlqs_server(), "rlqs_wrong_server",
                       std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1234));
      }

      // Set the domain name.
      proto_config_.set_domain("cloud_12345_67890_rlqs");
      proto_config_.mutable_bucket_matchers()->MergeFrom(matcher);

      // Construct a configuration proto for our filter and then re-write it
      // to JSON so that we can add it to the overall config
      envoy::config::listener::v3::Filter rate_limit_quota_filter;
      rate_limit_quota_filter.set_name("envoy.filters.http.rate_limit_quota");
      rate_limit_quota_filter.mutable_typed_config()->PackFrom(proto_config_);
      config_helper_.prependFilter(
          MessageUtil::getJsonStringFromMessageOrError(rate_limit_quota_filter));
    });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
  }

  // Send downstream client request.
  void
  sendClientRequest(const absl::flat_hash_map<std::string, std::string>* custom_headers = nullptr) {
    traffic_codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (custom_headers != nullptr) {
      for (auto const& pair : *custom_headers) {
        headers.addCopy(pair.first, pair.second);
      }
    }
    // Trigger responses from the autonomous upstream.
    headers.addCopy(AutonomousStream::RESPOND_AFTER_REQUEST_HEADERS, "yes");

    response_ = traffic_codec_client_->makeHeaderOnlyRequest(headers);
  }

  void cleanUp() {
    if (rlqs_connection_ != nullptr) {
      EXPECT_TRUE(rlqs_connection_->close());
      EXPECT_TRUE(rlqs_connection_->waitForDisconnect());
    }
    if (traffic_codec_client_ != nullptr) {
      traffic_codec_client_->close();
      EXPECT_TRUE(traffic_codec_client_->waitForDisconnect());
    }
    cleanupUpstreamAndDownstream();
  }

  void TearDown() override { cleanUp(); }

  bool expectDeniedRequest(int expected_status_code,
                           std::vector<std::pair<std::string, std::string>> expected_headers = {},
                           std::string expected_body = "") {
    if (!response_->waitForEndStream())
      return false;
    EXPECT_TRUE(response_->complete());
    EXPECT_EQ(response_->headers().getStatusValue(), absl::StrCat(expected_status_code));

    // Check for expected headers & body if set.
    for (const auto& [key, value] : expected_headers) {
      Http::HeaderMap::GetResult result = response_->headers().get(Http::LowerCaseString(key));
      if (result.empty()) {
        EXPECT_FALSE(result.empty());
        continue;
      }
      EXPECT_THAT(result[0]->value().getStringView(), testing::StrEq(value));
    }
    if (!expected_body.empty()) {
      EXPECT_THAT(response_->body(), testing::StrEq(expected_body));
    }
    // Don't call cleanupUpstreamAndDownstream() here as that will tear down the fake RLQS server's
    // connections.
    traffic_codec_client_->close();
    return true;
  }

  bool expectAllowedRequest() {
    if (!response_->waitForEndStream())
      return false;
    EXPECT_TRUE(response_->complete());

    EXPECT_EQ(response_->headers().getStatusValue(), "200");
    // Don't call cleanupUpstreamAndDownstream() here as that will tear down the fake RLQS server's
    // connections.
    traffic_codec_client_->close();
    return true;
  }

  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig
      proto_config_{};
  Network::Address::InstanceConstSharedPtr traffic_endpoint_;
  AutonomousUpstream* traffic_upstream_;
  Cluster* traffic_cluster_;
  IntegrationCodecClientPtr traffic_codec_client_;
  IntegrationStreamDecoderPtr response_;

  Network::Address::InstanceConstSharedPtr rlqs_endpoint_;
  FakeUpstream* rlqs_upstream_;
  FakeHttpConnectionPtr rlqs_connection_;
  FakeStreamPtr rlqs_stream_;
  Cluster* rlqs_cluster_;
  // TODO(bsurber): Implement report timing & usage aggregation based on each
  // bucket's reporting_interval field. Currently this is not supported and all
  // usage is reported on a hardcoded interval.
  int report_interval_sec_ = 5;
  RateLimitStrategy deny_all_strategy;
  RateLimitStrategy allow_all_strategy;
  // Prefer initialization around this default matcher if manipulations aren't needed.
  Matcher default_matcher_;

  // Access to static state, needed to reset between tests.
  GlobalTlsStores global_tls_stores_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing, RateLimitQuotaIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(RateLimitQuotaIntegrationTest, StartFailed) {
  SKIP_IF_GRPC_CLIENT(Grpc::ClientType::GoogleGrpc);
  initializeConfig(default_matcher_, false);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  sendClientRequest(&custom_headers);
  EXPECT_FALSE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_,
                                                     std::chrono::seconds(1)));
  EXPECT_TRUE(expectAllowedRequest());
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowEmptyResponse) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  rlqs_stream_->startGrpcStream();

  // Wait for initial usage report for the new bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Verify the response to downstream.
  ASSERT_TRUE(expectAllowedRequest());
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowResponseNotMatched) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  rlqs_stream_->startGrpcStream();

  // Wait for initial usage report for the new bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Build the response whose bucket ID doesn't match the sent report.
  RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  (*bucket_action->mutable_bucket_id()->mutable_bucket())
      .insert({"not_matched_key", "not_matched_value"});
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  expectAllowedRequest();
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowResponseMatched) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  rlqs_stream_->startGrpcStream();
  // Handle the request received by upstream.
  ASSERT_TRUE(expectAllowedRequest());

  // Wait for initial usage report for the new bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Build the response whose bucket ID matches the sent report.
  RateLimitQuotaResponse rlqs_response;
  custom_headers.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);
}

TEST_P(RateLimitQuotaIntegrationTest, TestBasicMetadataLogging) {
  initializeConfig(
      default_matcher_, true,
      "Whole Bucket ID=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_limit_quota.bucket)%\n"
      "Name=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_limit_quota.bucket:name)%");
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Wait for the first usage reports.
  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Build the response whose bucket ID matches the sent report.
  RateLimitQuotaResponse rlqs_response;
  custom_headers.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  ASSERT_TRUE(expectAllowedRequest());

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

TEST_P(RateLimitQuotaIntegrationTest, TestBasicPreviewMetadataLogging) {
  // Test metadata from both preview and non-preview buckets.
  Matcher preview_matcher = constructPreviewMatcher();

  BucketId preview_bucket_id;
  preview_bucket_id.mutable_bucket()->insert(
      {{"name", "prod"}, {"environment", "staging"}, {"group", "preview rule"}});

  // Set the preview matcher to DENY_ALL.
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
          .custom_bucket_id = preview_bucket_id,
      },
      preview_matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());
  // Set OnNoMatch to ALLOW_ALL.
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
      },
      preview_matcher.mutable_on_no_match());

  // Example of how to reference the well-known metadata structs & their KV pairs.
  initializeConfig(
      preview_matcher, true,
      "Whole Bucket ID=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_limit_quota.bucket)%\n"
      "Name=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_limit_quota.bucket:name)%\n"
      "Preview Bucket "
      "ID=%DYNAMIC_METADATA(envoy.extensions.http_filters.rate_limit_quota.preview_bucket)%");
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Autonomous upstream handles the request. Verify the response to downstream.
  EXPECT_TRUE(expectAllowedRequest());

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Wait for the first usage reports.
  RateLimitQuotaUsageReports reports1;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports1));
  RateLimitQuotaUsageReports reports2;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports2));
  rlqs_stream_->startGrpcStream();

  std::string log_output0 =
      HttpIntegrationTest::waitForAccessLog(HttpIntegrationTest::access_log_name_, 0, true);
  EXPECT_THAT(log_output0, testing::HasSubstr("Whole Bucket ID"));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"name\":\"prod\""));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"group\":\"envoy\""));
  EXPECT_THAT(log_output0, testing::HasSubstr("\"environment\":\"staging\""));
  std::string log_output1 =
      HttpIntegrationTest::waitForAccessLog(HttpIntegrationTest::access_log_name_, 1, true);
  EXPECT_THAT(log_output1, testing::HasSubstr("Name=prod"));
  std::string log_output2 =
      HttpIntegrationTest::waitForAccessLog(HttpIntegrationTest::access_log_name_, 2, true);
  EXPECT_THAT(log_output2, testing::HasSubstr("Preview Bucket ID"));
  EXPECT_THAT(log_output2, testing::HasSubstr("\"name\":\"prod\""));
  EXPECT_THAT(log_output2, testing::HasSubstr("\"group\":\"preview rule\""));
  EXPECT_THAT(log_output2, testing::HasSubstr("\"environment\":\"staging\""));

  cleanUp();
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowMultiSameRequest) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  for (int i = 0; i < 3; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Second downstream client request will not trigger the reports to RLQS
    // server since it is same as first request, which will find the entry in
    // the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();

      // Wait for initial usage report for the new bucket.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
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
    ASSERT_TRUE(expectAllowedRequest());

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowMultiDifferentRequest) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();

  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers = {
      {{"environment", "staging"}, {"group", "envoy"}},
      {{"environment", "staging"}, {"group", "envoy_1"}},
      {{"environment", "staging"}, {"group", "envoy_2"}}};
  int header_size = custom_headers.size();
  RateLimitQuotaResponse rlqs_response;
  RateLimitQuotaUsageReports expected_reports;
  expected_reports.set_domain("cloud_12345_67890_rlqs");

  // Send a request for each header permutation.
  for (int i = 0; i < header_size; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers[i]);
    // Expect a stream to open to the RLQS server with the first filter hit.
    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();
    }

    // Wait for initial usage report for the new bucket.
    RateLimitQuotaUsageReports initial_report;
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, initial_report));
    ASSERT_EQ(initial_report.bucket_quota_usages_size(), 1);
    EXPECT_EQ(initial_report.bucket_quota_usages(0).num_requests_allowed(), 1);

    // Add to the response & expected reports.
    absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers[i];
    custom_headers_cpy.insert({"name", "prod"});

    auto* bucket_action = rlqs_response.add_bucket_action();
    auto* expected_usage = expected_reports.add_bucket_quota_usages();
    EXPECT_EQ(initial_report.bucket_quota_usages(0).bucket_id().bucket().size(),
              custom_headers_cpy.size());
    for (const auto& [key, value] : custom_headers_cpy) {
      bucket_action->mutable_bucket_id()->mutable_bucket()->insert({key, value});
      expected_usage->mutable_bucket_id()->mutable_bucket()->insert({key, value});
      // Confirm that the initial report had the same, expected bucket id.
      EXPECT_EQ(initial_report.bucket_quota_usages(0).bucket_id().bucket().at(key), value);
    }
    expected_usage->set_num_requests_allowed(1);

    // Handle the request received by upstream.
    EXPECT_TRUE(expectAllowedRequest());

    // Send another request to tick up usage for a subsequent report.
    sendClientRequest(&custom_headers[i]);
    EXPECT_TRUE(expectAllowedRequest());
  }

  // Expect a report to be sent with all the buckets.
  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  const Protobuf::FieldDescriptor* time_elapsed_desc =
      RateLimitQuotaUsageReports::BucketQuotaUsage::GetDescriptor()->FindFieldByName(
          "time_elapsed");
  ASSERT_THAT(reports, ProtoEqIgnoringFieldAndOrdering(expected_reports, time_elapsed_desc));

  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestNoAssignmentDenyAll) {
  Matcher matcher = default_matcher_;
  // Set the FieldMatcher's no-assignment behavior to DENY_ALL.
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 3; ++i) {
    sendClientRequest(&custom_headers);

    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();

      // Expect an initial report.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The request is denied by no_assignment_behavior.
      EXPECT_EQ(usage.num_requests_allowed(), 0);
      EXPECT_EQ(usage.num_requests_denied(), 1);
    }

    // No response sent so filter should fallback to NoAssignmentBehavior
    // (deny-all here).
    // Verify the response to downstream.
    EXPECT_TRUE(expectDeniedRequest(429));
  }

  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // The request is denied by no_assignment_behavior.
  EXPECT_EQ(usage.num_requests_allowed(), 0);
  EXPECT_EQ(usage.num_requests_denied(), 2);
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestNoAssignmentDenyAllWithSettings) {
  Matcher matcher = default_matcher_;
  // Set deny_response_settings with custom values.
  DenyResponseSettings deny_response_settings;
  deny_response_settings.mutable_http_status()->set_code(envoy::type::v3::StatusCode::Forbidden);
  *deny_response_settings.mutable_http_body()->mutable_value() =
      "Denied by no-assignment behavior.";
  envoy::config::core::v3::HeaderValueOption* new_header =
      deny_response_settings.mutable_response_headers_to_add()->Add();
  new_header->mutable_header()->set_key("custom-denial-header-key");
  new_header->mutable_header()->set_value("custom-denial-header-value");

  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
          .deny_response_settings = deny_response_settings,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 3; ++i) {
    sendClientRequest(&custom_headers);

    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();

      // Expect an initial report.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The request is denied by no_assignment_behavior.
      EXPECT_EQ(usage.num_requests_allowed(), 0);
      EXPECT_EQ(usage.num_requests_denied(), 1);
    }

    // No response sent so filter should fallback to NoAssignmentBehavior
    // (deny-all here with a customized response code + body + headers).
    // Verify the response to downstream.
    EXPECT_TRUE(expectDeniedRequest(403,
                                    {{"custom-denial-header-key", "custom-denial-header-value"}},
                                    "Denied by no-assignment behavior."));
  }

  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // The request is denied by no_assignment_behavior.
  EXPECT_EQ(usage.num_requests_allowed(), 0);
  EXPECT_EQ(usage.num_requests_denied(), 2);
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestNoAssignmentDenyAllWithEmptyBodySettings) {
  // Set deny_response_settings with custom headers but no body.
  DenyResponseSettings deny_response_settings;
  deny_response_settings.mutable_http_status()->set_code(envoy::type::v3::StatusCode::Forbidden);
  envoy::config::core::v3::HeaderValueOption* new_header =
      deny_response_settings.mutable_response_headers_to_add()->Add();
  new_header->mutable_header()->set_key("custom-denial-header-key");
  new_header->mutable_header()->set_value("custom-denial-header-value");

  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
          .deny_response_settings = deny_response_settings,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 3; ++i) {
    sendClientRequest(&custom_headers);

    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first request.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();

      // Expect an initial report.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

      // Verify the usage report content.
      ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
      const auto& usage = reports.bucket_quota_usages(0);
      // The request is denied by no_assignment_behavior.
      EXPECT_EQ(usage.num_requests_allowed(), 0);
      EXPECT_EQ(usage.num_requests_denied(), 1);
    }

    // No response sent so filter should fallback to NoAssignmentBehavior
    // (deny-all here with a customized response code + body + headers).
    // Verify the response to downstream.
    EXPECT_TRUE(
        expectDeniedRequest(403, {{"custom-denial-header-key", "custom-denial-header-value"}}, ""));
  }

  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // The request is denied by no_assignment_behavior.
  EXPECT_EQ(usage.num_requests_allowed(), 0);
  EXPECT_EQ(usage.num_requests_denied(), 2);
}

TEST_P(RateLimitQuotaIntegrationTest, MultiDifferentRequestNoAssignementAllowAll) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();

  RateLimitQuotaUsageReports expected_reports;
  expected_reports.set_domain("cloud_12345_67890_rlqs");

  int total_num_of_request = 10;
  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers(
      total_num_of_request, {{"environment", "staging"}});
  std::vector<BucketId> expected_bucket_ids(total_num_of_request);
  std::string key_prefix = "envoy_";
  for (int i = 0; i < total_num_of_request; ++i) {
    // Prep 10 unique sets of headers.
    custom_headers[i].insert({"group", absl::StrCat(key_prefix, i)});

    // Each set of headers should be a separate bucket in the final reports.
    auto* expected_usage = expected_reports.add_bucket_quota_usages();
    for (const auto& [key, value] : custom_headers[i]) {
      expected_usage->mutable_bucket_id()->mutable_bucket()->insert({key, value});
    }
    expected_usage->mutable_bucket_id()->mutable_bucket()->insert({"name", "prod"});
    *expected_bucket_ids[i].mutable_bucket() = expected_usage->bucket_id().bucket();
    // Expect the request to each bucket to be allowed due to the no-assignment
    // behavior.
    expected_usage->set_num_requests_allowed(1);
  }

  for (int i = 0; i < total_num_of_request; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers[i]);
    // No RLQS server response is sent back in this test.

    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first reports.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
    }

    RateLimitQuotaUsageReports initial_report;
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, initial_report));

    ASSERT_EQ(initial_report.bucket_quota_usages_size(), 1);
    EXPECT_EQ(initial_report.bucket_quota_usages(0).num_requests_allowed(), 1);
    EXPECT_THAT(initial_report.bucket_quota_usages(0).bucket_id(), ProtoEq(expected_bucket_ids[i]));

    // Expect the request to be allowed due to the no-assignment behavior.
    EXPECT_TRUE(expectAllowedRequest());

    // Add more traffic to each bucket to test a subsequent report.
    sendClientRequest(&custom_headers[i]);
    EXPECT_TRUE(expectAllowedRequest());
  }

  // Expect a message to be sent with all the buckets' reports.
  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  const Protobuf::FieldDescriptor* time_elapsed_desc =
      RateLimitQuotaUsageReports::BucketQuotaUsage::GetDescriptor()->FindFieldByName(
          "time_elapsed");
  ASSERT_THAT(reports, ProtoEqIgnoringFieldAndOrdering(expected_reports, time_elapsed_desc));
}

// Test behaviors when a preview matcher executes before a non-preview matcher.
// The preview matcher should be matched & its action evaluated, but resulting
// deny decision should be ignored. The default matcher's bucket should instead
// also be evaluated & its allow decision respected.
TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestAllowAllPreviewDenyAll) {
  Matcher preview_matcher = constructPreviewMatcher();
  BucketId preview_bucket_id;
  preview_bucket_id.mutable_bucket()->insert(
      {{"name", "prod"}, {"environment", "staging"}, {"group", "preview rule"}});

  BucketId skipped_preview_id;
  skipped_preview_id.mutable_bucket()->insert(
      {{"name", "prod"}, {"environment", "staging"}, {"group", "another preview rule"}});

  // Set the previewed FieldMatcher to log the skipped denial decision but allow the request via the
  // on_no_match. The second preview FieldMatcher is ignored entirely as it would not have been hit
  // had all these Matchers been enforceable.
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
          .custom_bucket_id = preview_bucket_id,
      },
      preview_matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());
  // Copy the first preview matcher to a second with a different no_assignment behavior & bucket id.
  // We expect the second preview-mode matcher to be ignored entirely.
  auto* skipped_preview_matcher = preview_matcher.mutable_matcher_list()->mutable_matchers()->Add();
  skipped_preview_matcher->CopyFrom(preview_matcher.matcher_list().matchers(0));
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
          .custom_bucket_id = skipped_preview_id,
      },
      skipped_preview_matcher->mutable_on_match());
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
      },
      preview_matcher.mutable_on_no_match());

  initializeConfig(preview_matcher);
  initialize();

  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  rlqs_stream_->startGrpcStream();

  // Wait for initial usage report for the new bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Build the response.
  RateLimitQuotaResponse rlqs_response;
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

  // Autonomous upstream handles the request. Verify the response to downstream.
  EXPECT_TRUE(expectAllowedRequest());

  cleanUp();
}

TEST_P(RateLimitQuotaIntegrationTest, MultiDifferentRequestNoAssignmentDenyAll) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::DENY_ALL,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();

  RateLimitQuotaUsageReports expected_reports;
  expected_reports.set_domain("cloud_12345_67890_rlqs");

  int total_num_of_request = 5;
  std::vector<absl::flat_hash_map<std::string, std::string>> custom_headers(
      total_num_of_request, {{"environment", "staging"}});
  std::vector<BucketId> expected_bucket_ids(total_num_of_request);
  std::string key_prefix = "envoy_";
  for (int i = 0; i < total_num_of_request; ++i) {
    // Prep 5 unique sets of headers.
    custom_headers[i].insert({"group", absl::StrCat(key_prefix, i)});

    // Each set of headers should be a separate bucket in the final reports.
    auto* expected_usage = expected_reports.add_bucket_quota_usages();
    for (const auto& [key, value] : custom_headers[i]) {
      expected_usage->mutable_bucket_id()->mutable_bucket()->insert({key, value});
    }
    expected_usage->mutable_bucket_id()->mutable_bucket()->insert({"name", "prod"});
    *expected_bucket_ids[i].mutable_bucket() = expected_usage->bucket_id().bucket();
    // Expect the request to each bucket to be denied due to the no-assignment
    // behavior.
    expected_usage->set_num_requests_denied(1);
  }

  for (int i = 0; i < total_num_of_request; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers[i]);
    // No RLQS server response is sent back in this test.

    if (i == 0) {
      // Start the gRPC stream to RLQS server on the first reports.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
    }

    RateLimitQuotaUsageReports initial_report;
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, initial_report));

    ASSERT_EQ(initial_report.bucket_quota_usages_size(), 1);
    EXPECT_EQ(initial_report.bucket_quota_usages(0).num_requests_denied(), 1);
    EXPECT_THAT(initial_report.bucket_quota_usages(0).bucket_id(), ProtoEq(expected_bucket_ids[i]));

    // Expect the request to be allowed due to the no-assignment behavior.
    EXPECT_TRUE(expectDeniedRequest(429));

    // Add more traffic to each bucket to test a subsequent report.
    sendClientRequest(&custom_headers[i]);
    EXPECT_TRUE(expectDeniedRequest(429));
  }

  // Expect a message to be sent with all the buckets' reports.
  simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  const Protobuf::FieldDescriptor* time_elapsed_desc =
      RateLimitQuotaUsageReports::BucketQuotaUsage::GetDescriptor()->FindFieldByName(
          "time_elapsed");
  ASSERT_THAT(reports, ProtoEqIgnoringFieldAndOrdering(expected_reports, time_elapsed_desc));
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowPeriodicalReport) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Expect the RLQS stream to start.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // No-assignment behavior dictates that initial traffic should be allowed.
  EXPECT_TRUE(expectAllowedRequest());

  // Expect an initial report when traffic first hits the RLQS bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Verify the usage report content.
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // We only sent a single downstream client request and it was allowed.
  EXPECT_EQ(usage.num_requests_allowed(), 1);
  EXPECT_EQ(usage.num_requests_denied(), 0);
  // It is first report so the time_elapsed is 0.
  EXPECT_EQ(usage.time_elapsed().seconds(), 0);

  rlqs_stream_->startGrpcStream();

  // Build the response.
  RateLimitQuotaResponse rlqs_response;
  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();

  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // TODO(tyxia) Make interval configurable in the test.
  // As the sim-time progresses, watch reports trigger 10 times.
  for (int i = 0; i < 10; ++i) {
    // Show that different amounts of traffic are aggregated correctly (i + 1
    // requests).
    for (int t = 0; t <= i; ++t) {
      sendClientRequest(&custom_headers);
      EXPECT_TRUE(expectAllowedRequest());
    }

    // Advance the time by report_interval.
    simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
    // Checks that the rate limit server has received the periodical reports.
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

    // Verify the usage report content.
    ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
    const auto& usage = reports.bucket_quota_usages(0);
    // Report only represents the usage since last report.
    // In the periodical report case here, the number of request allowed and
    // denied is 0 since no new requests comes in.
    EXPECT_EQ(usage.num_requests_allowed(), i + 1);
    EXPECT_EQ(usage.num_requests_denied(), 0);
    // time_elapsed equals to periodical reporting interval.
    EXPECT_EQ(usage.time_elapsed().seconds(), report_interval_sec_);

    // Build the rlqs server response to avoid expiring assignments.
    RateLimitQuotaResponse rlqs_response2;
    auto* bucket_action2 = rlqs_response2.add_bucket_action();
    bucket_action2->mutable_quota_assignment_action()
        ->mutable_assignment_time_to_live()
        ->set_seconds(report_interval_sec_ * 10);
    bucket_action2->mutable_quota_assignment_action()
        ->mutable_rate_limit_strategy()
        ->set_blanket_rule(RateLimitStrategy::ALLOW_ALL);

    for (const auto& [key, value] : custom_headers_cpy) {
      (*bucket_action2->mutable_bucket_id()->mutable_bucket()).insert({key, value});
    }
    rlqs_stream_->sendGrpcMessage(rlqs_response2);
  }
}

TEST_P(RateLimitQuotaIntegrationTest, BasicFlowPeriodicalReportWithStreamClosed) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  // Send downstream client request to upstream.
  WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                        { sendClientRequest(&custom_headers); });

  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Expect an initial report when traffic first hits the RLQS bucket.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Verify the usage report content.
  ASSERT_THAT(reports.bucket_quota_usages_size(), 1);
  const auto& usage = reports.bucket_quota_usages(0);
  // We only sent a single downstream client request and it was allowed.
  EXPECT_EQ(usage.num_requests_allowed(), 1);
  EXPECT_EQ(usage.num_requests_denied(), 0);
  EXPECT_EQ(Protobuf::util::TimeUtil::DurationToSeconds(usage.time_elapsed()), 0);

  rlqs_stream_->startGrpcStream();

  // Build the response.
  RateLimitQuotaResponse rlqs_response;
  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});
  auto* bucket_action = rlqs_response.add_bucket_action();

  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  rlqs_stream_->sendGrpcMessage(rlqs_response);

  // Handle the request received by upstream.
  EXPECT_TRUE(expectAllowedRequest());

  // Trigger the report periodically.
  for (int i = 0; i < 6; ++i) {
    if (i == 2) {
      // Close the stream.
      ASSERT_FALSE(rlqs_stream_->waitForReset(std::chrono::milliseconds(0)));
      WAIT_FOR_LOG_CONTAINS("debug", "gRPC stream closed remotely with status", {
        rlqs_stream_->finishGrpcStream(Grpc::Status::Canceled);
        ASSERT_TRUE(rlqs_stream_->waitForReset());
      });
    }

    // Advance the time by report_interval.
    simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));

    if (i == 2) {
      // Stream should be restarted when next required for usage reporting.
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
      rlqs_stream_->startGrpcStream();
    }

    // Only perform rlqs server check and response before stream is remotely
    // closed.
    // Checks that the rate limit server has received the periodical reports.
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
              report_interval_sec_);

    // Build the rlqs server response.
    RateLimitQuotaResponse rlqs_response2;
    auto* bucket_action2 = rlqs_response2.add_bucket_action();

    for (const auto& [key, value] : custom_headers_cpy) {
      (*bucket_action2->mutable_bucket_id()->mutable_bucket()).insert({key, value});
    }
    WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                          { rlqs_stream_->sendGrpcMessage(rlqs_response2); });
  }
}

// RLQS filter is operating in non-blocking mode now, this test could be flaky
// until the stats are added to make the test behavior deterministic. (e.g.,
// wait for stats in the test). Waiting for logs mitigates this but is
// imperfect.
TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithTokenBucketThrottling) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int max_token = 1;
  int tokens_per_fill = 30;
  int fill_interval_sec = 60;
  std::chrono::milliseconds fill_one_token_in_ms((fill_interval_sec / tokens_per_fill) * 1000);

  // First  request: allowed; fail-open, default no assignment policy.
  // Second request: allowed; one token remaining, token bucket's max_token
  // is 1. Third  request: allowed; token bucket has been refilled by advancing
  // 2s. Fourth request: rejected; no token left and no token refilled. Fifth
  // request: allowed; token bucket has been refilled by advancing 2s. Sixth
  // request: rejected; no token left and no token refilled.
  for (int i = 0; i < 6; ++i) {
    // We advance time by 2s for 3rd and 5th requests so that token bucket can
    // be refilled.
    if (i == 2 || i == 4) {
      simTime().advanceTimeWait(std::chrono::milliseconds(fill_one_token_in_ms));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      ASSERT_TRUE(expectAllowedRequest());

      RateLimitQuotaUsageReports initial_reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, initial_reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
      }
      auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
      quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
      auto* strategy = quota_assignment->mutable_rate_limit_strategy();
      auto* token_bucket = strategy->mutable_token_bucket();
      token_bucket->set_max_tokens(max_token);
      token_bucket->mutable_tokens_per_fill()->set_value(30);
      token_bucket->mutable_fill_interval()->set_seconds(60);

      // Send the response from RLQS server.
      // Leave wiggle room as this does not have good 'finished' signaling yet.
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    } else if (i == 3 || i == 5) {
      // 4th and 6th request are throttled.
      ASSERT_TRUE(expectDeniedRequest(429));
    } else {
      ASSERT_TRUE(expectAllowedRequest());
    }
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithTokenBucketExpiration) {
  int max_token = 3;
  int tokens_per_fill = 1;
  int fill_interval_sec = 30;
  // Set expiration so existing TokenBucket will refill to 33%% between test
  // phases.
  int expiration_sec = 30;
  int expiration_max_token = 6;
  int fallback_expiration_sec = 20;

  RateLimitStrategy fallback_strategy;
  auto* fallback_tb_config = fallback_strategy.mutable_token_bucket();
  fallback_tb_config->set_max_tokens(expiration_max_token);
  fallback_tb_config->mutable_tokens_per_fill()->set_value(tokens_per_fill);
  fallback_tb_config->mutable_fill_interval()->set_seconds(fill_interval_sec);

  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
          .fallback_rate_limit_strategy = fallback_strategy,
          .fallback_ttl_sec = fallback_expiration_sec,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  // First request is allowed by the default-open & triggers bucket the
  // RLQS stream creation.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server on the first request.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));
  rlqs_stream_->startGrpcStream();

  // RLQS stream is triggered by first bucket creation.
  // The first request should be allowed by the no_assignment default-allow.
  ASSERT_TRUE(expectAllowedRequest());

  // First usage report is sent when the RLQS bucket is first hit.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  // Build the TokenBucket response.
  RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  bucket_action->mutable_bucket_id()->mutable_bucket()->insert(
      {{"name", "prod"}, {"environment", "staging"}, {"group", "envoy"}});
  auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
  quota_assignment->mutable_assignment_time_to_live()->set_seconds(expiration_sec);
  auto* strategy = quota_assignment->mutable_rate_limit_strategy();
  auto* token_bucket = strategy->mutable_token_bucket();
  token_bucket->set_max_tokens(max_token);
  token_bucket->mutable_tokens_per_fill()->set_value(tokens_per_fill);
  token_bucket->mutable_fill_interval()->set_seconds(fill_interval_sec);

  // Send the response from RLQS server & give a little time for assignments to
  // propagate.
  rlqs_stream_->sendGrpcMessage(rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));

  // The 2nd, 3rd and 4th requests are allowed by the token bucket assignment.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());

  // The 5th request should be denied by the token bucket assignment.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));

  // Advance time to trigger & wait for the token bucket assignment's
  // expiration.
  simTime().advanceTimeWait(std::chrono::seconds(expiration_sec));

  // The 6th & 7th requests should be allowed by the fallback TokenBucket as it
  // should initialize at 33% capacity.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());

  // The 8th request should be denied by the emptied, fallback TokenBucket.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));

  // Advance time to trigger & wait for the fallback assignment's expiration.
  simTime().advanceTimeWait(std::chrono::seconds(fallback_expiration_sec));

  // All remaining messages should be allowed by the default-allow.
  for (int i = 0; i < 3; ++i) {
    sendClientRequest(&custom_headers);
    ASSERT_TRUE(expectAllowedRequest());
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithTokenBucketReplacement) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int max_token = 2;
  int tokens_per_fill = 1;
  int fill_interval_sec = 1;
  int replacement_max_token = 6;
  int replacement_tokens_per_fill = 2;
  int replacement_fill_interval_sec = 1;

  // First request is allowed by the default-open & triggers bucket creation +
  // initial usage reporting.
  sendClientRequest(&custom_headers);

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Expect an initial report when the RLQS bucket is first hit.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();

  // Expect default allow-all for the first request.
  ASSERT_TRUE(expectAllowedRequest());

  // Build the TokenBucket response.
  RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  bucket_action->mutable_bucket_id()->mutable_bucket()->insert(
      {{"name", "prod"}, {"environment", "staging"}, {"group", "envoy"}});
  auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
  quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
  auto* strategy = quota_assignment->mutable_rate_limit_strategy();
  auto* token_bucket = strategy->mutable_token_bucket();
  token_bucket->set_max_tokens(max_token);
  token_bucket->mutable_tokens_per_fill()->set_value(tokens_per_fill);
  token_bucket->mutable_fill_interval()->set_seconds(fill_interval_sec);

  // Send the response from RLQS server.
  rlqs_stream_->sendGrpcMessage(rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));

  // Initial requests are allowed by the token bucket assignment.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  // Until the bucket is empty, then requests are denied.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));

  // Check that a single token fills after the fill interval.
  simTime().advanceTimeWait(std::chrono::seconds(fill_interval_sec));
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));

  // Allow for a 50% refill of the TokenBucket again before replacing it.
  simTime().advanceTimeWait(std::chrono::seconds(fill_interval_sec));

  // Prep a response to replace the existing TokenBucket with a larger one.
  RateLimitQuotaResponse replacement_rlqs_response(rlqs_response);
  auto* replacement_token_bucket = replacement_rlqs_response.mutable_bucket_action(0)
                                       ->mutable_quota_assignment_action()
                                       ->mutable_rate_limit_strategy()
                                       ->mutable_token_bucket();
  replacement_token_bucket->set_max_tokens(replacement_max_token);
  replacement_token_bucket->mutable_tokens_per_fill()->set_value(replacement_tokens_per_fill);
  replacement_token_bucket->mutable_fill_interval()->set_seconds(replacement_fill_interval_sec);

  // Send the response to update the TokenBucket.
  rlqs_stream_->sendGrpcMessage(replacement_rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));

  // Expect the new TokenBucket to initialize at 50% capacity, based on the
  // existing TokenBucket.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  // Empty after 50%, not max_tokens.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));

  // Test new TokenBucket's refill rate.
  simTime().advanceTimeWait(std::chrono::seconds(fill_interval_sec));
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectAllowedRequest());
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(expectDeniedRequest(429));
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsupportedStrategy) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 2; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Handle the request received by upstream. All requests will be allowed
    // since the strategy is not supported.
    EXPECT_TRUE(expectAllowedRequest());

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      // Expect an initial report when the RLQS bucket is first hit.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
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
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }

    cleanUp();
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsetStrategy) {
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 2; ++i) {
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Handle the request received by upstream. All requests will be allowed
    // since the strategy is not set.
    EXPECT_TRUE(expectAllowedRequest());

    // Only first downstream client request will trigger the reports to RLQS
    // server as the subsequent requests will find the entry in the cache.
    if (i == 0) {
      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      // Expect an initial report when the RLQS bucket is first hit.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();
      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(120);
      }

      // Send the response from RLQS server.
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiRequestWithUnsupportedDefaultAction) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .unsupported_no_assignment_strategy = true,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  // Send downstream client request to upstream.
  sendClientRequest(&custom_headers);

  // Handle the request received by upstream. All requests will be allowed
  // since the strategy is not set.
  EXPECT_TRUE(expectAllowedRequest());

  // Start the gRPC stream to RLQS server.
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Expect an initial report when the RLQS bucket is first hit.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpiredAssignmentDeny) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .fallback_rate_limit_strategy = deny_all_strategy,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int expiration_secs = 10;
  for (int i = 0; i < 2; ++i) {
    // Advance the time to make cached assignment expired.
    if (i == 1) {
      simTime().advanceTimeWait(std::chrono::seconds(expiration_secs));
    }
    // Send downstream client request to upstream.

    // 2nd downstream client request will not trigger the reports to RLQS server
    // since it is same as first request, which will find the entry in the
    // cache.
    if (i > 0) {
      sendClientRequest(&custom_headers);
    } else {
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { sendClientRequest(&custom_headers); });

      // Start the gRPC stream to RLQS server.
      ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
      ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

      // Expect an initial report when the RLQS bucket is first hit.
      RateLimitQuotaUsageReports reports;
      ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      rlqs_stream_->startGrpcStream();

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();

      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(expiration_secs);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
      }

      // Send the response from RLQS server and wait for response processing to
      // finish for test consistency.
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }

    // 2nd request is throttled because the assignment has expired and
    // expired assignment behavior is DENY_ALL.
    if (i == 1) {
      EXPECT_TRUE(expectDeniedRequest(429));
    } else {
      EXPECT_TRUE(expectAllowedRequest());
    }
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpiredAssignmentAllow) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .fallback_rate_limit_strategy = allow_all_strategy,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};
  int expiration_secs = 2;
  for (int i = 0; i < 3; ++i) {
    // Advance the time to make cached assignment expired.
    if (i == 1) {
      simTime().advanceTimeWait(std::chrono::seconds(expiration_secs));
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
        ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        // Expect an initial report when the RLQS bucket is first hit.
        RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();
      } else {
        // 3rd request won't start gRPC stream again since it is kept open and
        // the usage will be aggregated instead of spawning an immediate report.
        RateLimitQuotaUsageReports reports;
        ASSERT_FALSE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
      }

      // Build the response.
      RateLimitQuotaResponse rlqs_response;
      absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
      custom_headers_cpy.insert({"name", "prod"});
      auto* bucket_action = rlqs_response.add_bucket_action();

      for (const auto& [key, value] : custom_headers_cpy) {
        (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
        auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
        quota_assignment->mutable_assignment_time_to_live()->set_seconds(expiration_secs);
        auto* strategy = quota_assignment->mutable_rate_limit_strategy();
        strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::ALLOW_ALL);
      }

      // Send the response from RLQS server and wait for response processing to
      // finish for test consistency.
      WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                            { rlqs_stream_->sendGrpcMessage(rlqs_response); });
    }

    // Even though assignment was expired on 2nd request, the request is still
    // allowed because the expired assignment behavior is ALLOW_ALL.
    EXPECT_TRUE(expectAllowedRequest());
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpirationToDefaultDeny) {
  Matcher matcher = default_matcher_;
  Manipulations manipulations = {
      .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
      .fallback_rate_limit_strategy = deny_all_strategy,
  };
  manipulateOnMatch(manipulations,
                    matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  for (int i = 0; i < 4; ++i) {
    // Advance the time to make cached assignment expired.
    if (i > 1) {
      simTime().advanceTimeWait(std::chrono::seconds(kFallbackTtlSecDefault));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Query 1: ALLOW_ALL by default. Query 2: DENY_ALL by assignment.
    // Query 3: DENY_ALL by assignment expiration. Query 4: ALLOW_ALL by
    // default.
    if (i == 0 || i == 3) {
      // Handle the request received by upstream.
      expectAllowedRequest();
      if (i == 0) {
        // Expect a gRPC stream to the RLQS server opened with the first bucket.
        ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        // Expect an initial report when the RLQS bucket is first hit.
        RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();

        // Build the response.
        RateLimitQuotaResponse rlqs_response;
        absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
        custom_headers_cpy.insert({"name", "prod"});
        auto* bucket_action = rlqs_response.add_bucket_action();

        for (const auto& [key, value] : custom_headers_cpy) {
          (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
          auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
          quota_assignment->mutable_assignment_time_to_live()->set_seconds(kFallbackTtlSecDefault);
          auto* strategy = quota_assignment->mutable_rate_limit_strategy();
          strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }

        // Send the response from RLQS server.
        WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                              { rlqs_stream_->sendGrpcMessage(rlqs_response); });
      }
    } else {
      expectDeniedRequest(429);
    }
  }
}

TEST_P(RateLimitQuotaIntegrationTest, MultiSameRequestWithExpirationWithoutFallback) {
  Matcher matcher = default_matcher_;
  manipulateOnMatch(
      {
          .no_assignment_blanket_rule = RateLimitStrategy::ALLOW_ALL,
      },
      matcher.mutable_matcher_list()->mutable_matchers(0)->mutable_on_match());

  initializeConfig(matcher);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  int expiration_secs = 15;
  for (int i = 0; i < 3; ++i) {
    // Advance the time to make cached assignment expired.
    if (i > 1) {
      simTime().advanceTimeWait(std::chrono::seconds(expiration_secs));
    }
    // Send downstream client request to upstream.
    sendClientRequest(&custom_headers);

    // Query 1: ALLOW_ALL by default. Query 2: DENY_ALL by assignment.
    // Query 3: DENY_ALL by assignment expiration. Query 4: ALLOW_ALL by
    // default.
    if (i == 0 || i == 2) {
      // Handle the request received by upstream.
      EXPECT_TRUE(expectAllowedRequest());

      if (i == 0) {
        // Start the gRPC stream to RLQS server & send the initial report.
        ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
        ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

        // Expect an initial report when the RLQS bucket is first hit.
        RateLimitQuotaUsageReports reports;
        ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
        rlqs_stream_->startGrpcStream();

        // Build the response.
        RateLimitQuotaResponse rlqs_response;
        absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
        custom_headers_cpy.insert({"name", "prod"});
        auto* bucket_action = rlqs_response.add_bucket_action();

        for (const auto& [key, value] : custom_headers_cpy) {
          (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
          auto* quota_assignment = bucket_action->mutable_quota_assignment_action();
          quota_assignment->mutable_assignment_time_to_live()->set_seconds(expiration_secs);
          auto* strategy = quota_assignment->mutable_rate_limit_strategy();
          strategy->set_blanket_rule(envoy::type::v3::RateLimitStrategy::DENY_ALL);
        }

        // Send the response from RLQS server.
        WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                              { rlqs_stream_->sendGrpcMessage(rlqs_response); });
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
  initializeConfig(default_matcher_);
  HttpIntegrationTest::initialize();
  absl::flat_hash_map<std::string, std::string> custom_headers = {{"environment", "staging"},
                                                                  {"group", "envoy"}};

  absl::flat_hash_map<std::string, std::string> custom_headers_cpy = custom_headers;
  custom_headers_cpy.insert({"name", "prod"});

  // Send first request & expect a new RLQS stream.
  sendClientRequest(&custom_headers);
  ASSERT_TRUE(rlqs_upstream_->waitForHttpConnection(*dispatcher_, rlqs_connection_));
  ASSERT_TRUE(rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_stream_));

  // Expect an initial report when the RLQS bucket is first hit.
  RateLimitQuotaUsageReports reports;
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
  rlqs_stream_->startGrpcStream();
  EXPECT_EQ(reports.bucket_quota_usages_size(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_allowed(), 1);
  EXPECT_EQ(reports.bucket_quota_usages(0).num_requests_denied(), 0);

  // Expect the first request to be allowed.
  ASSERT_TRUE(expectAllowedRequest());

  // Build an abandon-action response.
  RateLimitQuotaResponse rlqs_response;
  auto* bucket_action = rlqs_response.add_bucket_action();
  for (const auto& [key, value] : custom_headers_cpy) {
    (*bucket_action->mutable_bucket_id()->mutable_bucket()).insert({key, value});
  }
  bucket_action->mutable_abandon_action();
  // Send the response from RLQS server.
  WAIT_FOR_LOG_CONTAINS("debug", "RLQS buckets cache written to TLS.",
                        { rlqs_stream_->sendGrpcMessage(rlqs_response); });

  // Expect the next report to be empty, since the cache entry was removed, but
  // allow for retries in case the response hasn't processed yet.
  bool empty_report = false;
  for (int i = 0; i < 5 && !empty_report; ++i) {
    simTime().advanceTimeWait(std::chrono::seconds(report_interval_sec_));
    ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));
    empty_report = reports.bucket_quota_usages().empty();
  }
  ASSERT_TRUE(empty_report);

  // Send a second request & expect it to be allowed while the bucket is
  // recreated in the cache.
  sendClientRequest(&custom_headers);

  // Expect a second, initial report when the RLQS bucket's cache is recreated.
  ASSERT_TRUE(rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

  ASSERT_TRUE(expectAllowedRequest());
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
