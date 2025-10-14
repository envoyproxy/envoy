#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/network/connection.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

#include "source/extensions/filters/http/rate_limit_quota/filter_persistence.h"

#include "test/common/http/common.h"
#include "test/integration/autonomous_upstream.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/http_integration.h"
#include "test/integration/integration_stream_decoder.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {
namespace {

using Envoy::ProtoEq;
using envoy::config::cluster::v3::Cluster;
using envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;
using envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
using envoy::service::rate_limit_quota::v3::BucketId;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse;
using envoy::service::rate_limit_quota::v3::RateLimitQuotaUsageReports;
using Protobuf::util::MessageDifferencer;

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

static constexpr char kDefaultRateLimitQuotaFilter[] = R"EOF(
name: "envoy.filters.http.rate_limit_quota"
typed_config:
  "@type": "type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaFilterConfig"
  rlqs_server:
    envoy_grpc:
      cluster_name: "rlqs_upstream_0"
  domain: "test_domain"
  bucket_matchers:
    matcher_list:
      matchers:
        predicate:
          single_predicate:
            input:
              typed_config:
                "@type": type.googleapis.com/envoy.type.matcher.v3.HttpRequestHeaderMatchInput
                header_name: environment
              name: "HttpRequestHeaderMatchInput"
            value_match:
              exact: staging
        on_match:
          action:
            name: rate_limit_quota
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings
              bucket_id_builder:
                bucket_id_builder:
                  "test_key_1":
                      string_value: "test_value_1"
                  "test_key_2":
                      string_value: "test_value_2"
              no_assignment_behavior:
                fallback_rate_limit:
                  blanket_rule: ALLOW_ALL
              reporting_interval: 5s
    on_no_match:
      action:
        name: rate_limit_quota
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings"
          bucket_id_builder:
            bucket_id_builder:
              "on_no_match_key":
                string_value: "on_no_match_value"
          no_assignment_behavior:
            fallback_rate_limit:
              blanket_rule: ALLOW_ALL
          reporting_interval: 5s
)EOF";

class FilterPersistenceTest : public Event::TestUsingSimulatedTime,
                              public HttpIntegrationTest,
                              public Grpc::GrpcClientIntegrationParamTest {
protected:
  FilterPersistenceTest() : HttpIntegrationTest(Http::CodecType::HTTP2, ipVersion()) {
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setUpIntegrationTest();
  }

  void setUpIntegrationTest() {
    config_helper_.addConfigModifier(
        [&]([[maybe_unused]] envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          // Start the server with a default RLQS filter that will then be
          // updated by LDS / xDS in the test cases.
          config_helper_.prependFilter(kDefaultRateLimitQuotaFilter);
        });
    setUpstreamProtocol(Http::CodecType::HTTP2);
    setDownstreamProtocol(Http::CodecType::HTTP2);
    HttpIntegrationTest::initialize();
  }

  // The RLQS upstream shouldn't be autonomous as it will handle the long-lived
  // RLQS stream.
  void createUpstreams() override {
    setUpstreamCount(3);

    autonomous_upstream_ = true;
    traffic_endpoint_ = upstream_address_fn_(0);
    createUpstream(traffic_endpoint_, upstreamConfig());
    traffic_upstream_ = dynamic_cast<AutonomousUpstream*>(fake_upstreams_[0].get());

    autonomous_upstream_ = false;
    // Testing requires multiple RLQS upstream targets.
    for (int i = 0; i < 2; ++i) {
      FakeRlqsUpstreamRefs rlqs_upstream{};
      rlqs_upstream.rlqs_endpoint_ = upstream_address_fn_(i + 1);
      createUpstream(rlqs_upstream.rlqs_endpoint_, upstreamConfig());
      rlqs_upstream.rlqs_upstream_ = fake_upstreams_[i + 1].get();

      rlqs_upstreams_.push_back(std::move(rlqs_upstream));
    }

    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      traffic_cluster_ = bootstrap.mutable_static_resources()->mutable_clusters(0);
      for (size_t i = 0; i < rlqs_upstreams_.size(); ++i) {
        FakeRlqsUpstreamRefs& rlqs_upstream_refs = rlqs_upstreams_.at(i);
        rlqs_upstream_refs.rlqs_cluster_ = bootstrap.mutable_static_resources()->add_clusters();

        rlqs_upstream_refs.rlqs_cluster_->MergeFrom(bootstrap.static_resources().clusters(0));

        rlqs_upstream_refs.rlqs_cluster_->set_name(absl::StrCat("rlqs_upstream_", i));
      }
    });
    tls_store_emptied_ = std::make_unique<absl::Notification>();
    GlobalTlsStores::registerEmptiedCb([&]() { tls_store_emptied_->Notify(); });
  }

  void updateConfigInPlace(std::function<void(envoy::config::bootstrap::v3::Bootstrap&)> modifier) {
    // update_success starts at 1 after the initial server configuration.
    test_server_->waitForCounterEq("listener_manager.lds.update_success", config_updates_ + 1);
    test_server_->waitForCounterEq("listener_manager.listener_modified", config_updates_);
    test_server_->waitForCounterEq("listener_manager.listener_in_place_updated", config_updates_);

    ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
    new_config_helper.addConfigModifier(modifier);
    new_config_helper.setLds(absl::StrCat(config_updates_ + 1));

    test_server_->waitForCounterEq("listener_manager.lds.update_success", config_updates_ + 2);
    test_server_->waitForCounterEq("listener_manager.listener_modified", config_updates_ + 1);
    test_server_->waitForCounterEq("listener_manager.listener_in_place_updated",
                                   config_updates_ + 1);
    test_server_->waitForGaugeEq("listener_manager.total_filter_chains_draining", 0);
    config_updates_++;
  }

  bool waitForAllTlsStoreDeletions() {
    if (tls_store_emptied_ == nullptr) {
      // Never initialized the TLS Store.
      return true;
    }
    return tls_store_emptied_->WaitForNotificationWithTimeout(absl::Seconds(3));
  }

  void wipeFilters() {
    updateConfigInPlace([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
      auto* hcm_filter = listener->mutable_filter_chains(0)->mutable_filters(0);
      HttpConnectionManager hcm_config;
      hcm_filter->mutable_typed_config()->UnpackTo(&hcm_config);
      hcm_config.clear_http_filters();
      hcm_filter->mutable_typed_config()->PackFrom(hcm_config);
    });
    // Wait for all TLS stores to be deleted now that the filter factories are gone.
    ASSERT_TRUE(waitForAllTlsStoreDeletions());
  }

  void cleanUp() {
    wipeFilters();
    for (auto& rlqs_upstream : rlqs_upstreams_) {
      if (rlqs_upstream.rlqs_connection_ != nullptr) {
        ASSERT_TRUE(rlqs_upstream.rlqs_connection_->close());
        ASSERT_TRUE(rlqs_upstream.rlqs_connection_->waitForDisconnect());
      }
    }
    cleanupUpstreamAndDownstream();
  }

  void TearDown() override { cleanUp(); }

  // Send a request through the envoy & possibly to traffic_upstream_. Returns
  // the response's status code.
  std::string
  sendRequest(const absl::flat_hash_map<std::string, std::string>* custom_headers = nullptr) {
    auto codec_client = makeHttpConnection(makeClientConnection(lookupPort("http")));

    Http::TestRequestHeaderMapImpl headers;
    HttpTestUtility::addDefaultHeaders(headers);
    if (custom_headers != nullptr) {
      for (auto const& pair : *custom_headers) {
        headers.addCopy(pair.first, pair.second);
      }
    }
    // Trigger responses from the autonomous upstream.
    headers.addCopy(AutonomousStream::RESPOND_AFTER_REQUEST_HEADERS, "yes");

    IntegrationStreamDecoderPtr response = codec_client->makeHeaderOnlyRequest(headers);
    bool stream_ended = response->waitForEndStream();

    codec_client->close();
    if (!stream_ended) {
      return "";
    }
    EXPECT_TRUE(response->complete());
    return std::string(response->headers().getStatusValue());
  }

  void expectRlqsUsageReports(int upstream_index,
                              const RateLimitQuotaUsageReports& expected_reports,
                              bool expect_new_stream = false) {
    FakeRlqsUpstreamRefs& rlqs_refs = rlqs_upstreams_.at(upstream_index);
    if (expect_new_stream) {
      ASSERT_TRUE(rlqs_refs.rlqs_upstream_->waitForHttpConnection(*dispatcher_,
                                                                  rlqs_refs.rlqs_connection_));
      ASSERT_TRUE(
          rlqs_refs.rlqs_connection_->waitForNewStream(*dispatcher_, rlqs_refs.rlqs_stream_));
      rlqs_refs.rlqs_stream_->startGrpcStream();
    }

    RateLimitQuotaUsageReports reports;
    ASSERT_TRUE(rlqs_refs.rlqs_stream_->waitForGrpcMessage(*dispatcher_, reports));

    // Ignore time_elapsed as it is often not deterministic.
    const Protobuf::FieldDescriptor* time_elapsed_desc =
        RateLimitQuotaUsageReports::BucketQuotaUsage::GetDescriptor()->FindFieldByName(
            "time_elapsed");
    ASSERT_THAT(reports, ProtoEqIgnoringFieldAndOrdering(expected_reports, time_elapsed_desc));
  }

  // Can only be called after the RLQS stream has been established with a RLQS
  // usage report.
  void sendRlqsResponse(int upstream_index, const RateLimitQuotaResponse& rlqs_response) {
    rlqs_upstreams_.at(upstream_index).rlqs_stream_->sendGrpcMessage(rlqs_response);
  }

  void cleanupRlqsStream(int upstream_index) {
    FakeRlqsUpstreamRefs& rlqs_refs = rlqs_upstreams_.at(upstream_index);
    if (rlqs_refs.rlqs_connection_ != nullptr) {
      ASSERT_TRUE(rlqs_refs.rlqs_connection_->close());
      ASSERT_TRUE(rlqs_refs.rlqs_connection_->waitForDisconnect());
      rlqs_refs.rlqs_connection_ = nullptr;
      rlqs_refs.rlqs_stream_ = nullptr;
    }
    absl::SleepFor(absl::Seconds(1));
  }

  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig
      rlqs_filter_config_{};

  struct FakeRlqsUpstreamRefs {
    Network::Address::InstanceConstSharedPtr rlqs_endpoint_ = nullptr;
    FakeUpstream* rlqs_upstream_ = nullptr;
    // Each FakeUpstream can only handle 1 active stream at a time, so we just
    // keep track of the most recent connection & stream.
    FakeHttpConnectionPtr rlqs_connection_ = nullptr;
    FakeStreamPtr rlqs_stream_ = nullptr;
    Cluster* rlqs_cluster_ = nullptr;
  };
  std::vector<FakeRlqsUpstreamRefs> rlqs_upstreams_{};

  Network::Address::InstanceConstSharedPtr traffic_endpoint_ = nullptr;
  AutonomousUpstream* traffic_upstream_ = nullptr;
  Cluster* traffic_cluster_ = nullptr;

  int config_updates_ = 0;

  std::unique_ptr<absl::Notification> tls_store_emptied_ = nullptr;
};
INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDeferredProcessing, FilterPersistenceTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(FilterPersistenceTest, TestPersistenceWithLdsUpdates) {
  RateLimitQuotaUsageReports expected_reports;
  TestUtility::loadFromYaml(R"EOF(
domain: "test_domain"
bucket_quota_usages:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  num_requests_allowed: 1
)EOF",
                            expected_reports);
  // The first request should trigger an immediate usage report. The
  // no-assignment behavior is ALLOW_ALL so the first request should be allowed.
  absl::flat_hash_map<std::string, std::string> headers = {{"environment", "staging"}};
  ASSERT_EQ(sendRequest(&headers), "200");
  expectRlqsUsageReports(0, expected_reports, true);

  RateLimitQuotaResponse rlqs_response;
  TestUtility::loadFromYaml(R"EOF(
bucket_action:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  quota_assignment_action:
    assignment_time_to_live:
      seconds: 120
    rate_limit_strategy:
      blanket_rule: DENY_ALL
)EOF",
                            rlqs_response);
  // RLQS response explicitly sets the cache to DENY_ALL.
  sendRlqsResponse(0, rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));
  ASSERT_EQ(sendRequest(&headers), "429");

  // Send an LDS update and make sure that the cache persisted. The cached
  // DENY_ALL assignment should be hit, and the filter's new
  // deny_response_settings should set the response code to 403.
  updateConfigInPlace([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    auto* hcm_filter = listener->mutable_filter_chains(0)->mutable_filters(0);
    HttpConnectionManager hcm_config;
    hcm_filter->mutable_typed_config()->UnpackTo(&hcm_config);

    auto* rlqs_filter = hcm_config.mutable_http_filters(0);
    RateLimitQuotaFilterConfig rlqs_filter_config;
    rlqs_filter->mutable_typed_config()->UnpackTo(&rlqs_filter_config);

    // Change the deny_response_settings to send 403 status codes.
    auto* on_match_action = rlqs_filter_config.mutable_bucket_matchers()
                                ->mutable_matcher_list()
                                ->mutable_matchers(0)
                                ->mutable_on_match()
                                ->mutable_action();
    RateLimitQuotaBucketSettings on_match_settings;
    on_match_action->mutable_typed_config()->UnpackTo(&on_match_settings);
    on_match_settings.mutable_deny_response_settings()->mutable_http_status()->set_code(
        envoy::type::v3::StatusCode::Forbidden);

    // Re-pack in reverse order.
    on_match_action->mutable_typed_config()->PackFrom(on_match_settings);
    rlqs_filter->mutable_typed_config()->PackFrom(rlqs_filter_config);
    hcm_filter->mutable_typed_config()->PackFrom(hcm_config);
  });
  ASSERT_EQ(sendRequest(&headers), "403");
}

TEST_P(FilterPersistenceTest, TestPersistenceWithLdsUpdateToNewDomain) {
  RateLimitQuotaUsageReports expected_reports;
  TestUtility::loadFromYaml(R"EOF(
domain: "test_domain"
bucket_quota_usages:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  num_requests_allowed: 1
)EOF",
                            expected_reports);
  // The first request should trigger an immediate usage report. The
  // no-assignment behavior is ALLOW_ALL so the first request should be allowed.
  absl::flat_hash_map<std::string, std::string> headers = {{"environment", "staging"}};
  ASSERT_EQ(sendRequest(&headers), "200");
  expectRlqsUsageReports(0, expected_reports, true);

  RateLimitQuotaResponse rlqs_response;
  TestUtility::loadFromYaml(R"EOF(
bucket_action:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  quota_assignment_action:
    assignment_time_to_live:
      seconds: 120
    rate_limit_strategy:
      blanket_rule: DENY_ALL
)EOF",
                            rlqs_response);
  // RLQS response explicitly sets the cache to DENY_ALL.
  sendRlqsResponse(0, rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));
  ASSERT_EQ(sendRequest(&headers), "429");

  // Send an LDS update with a new filter domain. A different domain or RLQS
  // server target should not match to the same persistent cache, so this should
  // trigger creation of a new filter factory that will create a new, global
  // RLQS client, quota assignment cache, etc.
  updateConfigInPlace([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    auto* hcm_filter = listener->mutable_filter_chains(0)->mutable_filters(0);
    HttpConnectionManager hcm_config;
    hcm_filter->mutable_typed_config()->UnpackTo(&hcm_config);

    auto* rlqs_filter = hcm_config.mutable_http_filters(0);
    RateLimitQuotaFilterConfig rlqs_filter_config;
    rlqs_filter->mutable_typed_config()->UnpackTo(&rlqs_filter_config);

    // Change the filter's top-level domain.
    rlqs_filter_config.set_domain("new_domain");

    // Re-pack in reverse order.
    rlqs_filter->mutable_typed_config()->PackFrom(rlqs_filter_config);
    hcm_filter->mutable_typed_config()->PackFrom(hcm_config);
  });

  // With an entirely fresh quota cache state & RLQS stream again, the next
  // request should be allowed & trigger an initial usage report.
  // Cleanup of the previous RLQS stream is needed as the same FakeUpstream can
  // only handle 1 active stream at a time.
  cleanupRlqsStream(0);
  ASSERT_EQ(sendRequest(&headers), "200");

  RateLimitQuotaUsageReports expected_reports_2 = expected_reports;
  expected_reports_2.set_domain("new_domain");
  expectRlqsUsageReports(0, expected_reports_2, true);

  // RLQS response explicitly sets the new cache to DENY_ALL.
  sendRlqsResponse(0, rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));
  ASSERT_EQ(sendRequest(&headers), "429");
}

TEST_P(FilterPersistenceTest, TestPersistenceWithLdsUpdateToNewRlqsServer) {
  RateLimitQuotaUsageReports expected_reports;
  TestUtility::loadFromYaml(R"EOF(
domain: "test_domain"
bucket_quota_usages:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  num_requests_allowed: 1
)EOF",
                            expected_reports);
  // The first request should trigger an immediate usage report. The
  // no-assignment behavior is ALLOW_ALL so the first request should be allowed.
  absl::flat_hash_map<std::string, std::string> headers = {{"environment", "staging"}};
  ASSERT_EQ(sendRequest(&headers), "200");
  expectRlqsUsageReports(0, expected_reports, true);

  RateLimitQuotaResponse rlqs_response;
  TestUtility::loadFromYaml(R"EOF(
bucket_action:
  bucket_id:
    bucket:
      "test_key_1":
        "test_value_1"
      "test_key_2":
        "test_value_2"
  quota_assignment_action:
    assignment_time_to_live:
      seconds: 120
    rate_limit_strategy:
      blanket_rule: DENY_ALL
)EOF",
                            rlqs_response);
  // RLQS response explicitly sets the cache to DENY_ALL.
  sendRlqsResponse(0, rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));
  ASSERT_EQ(sendRequest(&headers), "429");

  // Send an LDS update with a new filter domain. A different domain or RLQS
  // server target should not match to the same persistent cache, so this should
  // trigger creation of a new filter factory that will create a new, global
  // RLQS client, quota assignment cache, etc.
  updateConfigInPlace([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
    auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);

    auto* hcm_filter = listener->mutable_filter_chains(0)->mutable_filters(0);
    HttpConnectionManager hcm_config;
    hcm_filter->mutable_typed_config()->UnpackTo(&hcm_config);

    auto* rlqs_filter = hcm_config.mutable_http_filters(0);
    RateLimitQuotaFilterConfig rlqs_filter_config;
    rlqs_filter->mutable_typed_config()->UnpackTo(&rlqs_filter_config);

    // Change the filter's top-level RLQS server target.
    rlqs_filter_config.mutable_rlqs_server()->mutable_envoy_grpc()->set_cluster_name(
        "rlqs_upstream_1");

    // Re-pack in reverse order.
    rlqs_filter->mutable_typed_config()->PackFrom(rlqs_filter_config);
    hcm_filter->mutable_typed_config()->PackFrom(hcm_config);
  });

  ASSERT_EQ(sendRequest(&headers), "200");

  // Expect a duplicate, initial report on the new stream.
  expectRlqsUsageReports(1, expected_reports, true);

  // RLQS response explicitly sets the new cache to DENY_ALL.
  sendRlqsResponse(1, rlqs_response);
  absl::SleepFor(absl::Seconds(0.5));
  ASSERT_EQ(sendRequest(&headers), "429");
}

} // namespace
} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
