#pragma once

#include <chrono>

#include "source/common/http/context_impl.h"
#include "source/common/router/router.h"
#include "source/common/stream_info/uint32_accessor_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {

using ::testing::NiceMock;

class RouterTestFilter : public Filter {
public:
  using Filter::Filter;
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&, const VirtualCluster*,
                                 RouteStatsContextOptRef, Runtime::Loader&,
                                 Random::RandomGenerator&, Event::Dispatcher&, TimeSource&,
                                 Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    if (reject_all_hosts_) {
      // Set up RetryState to always reject the host
      ON_CALL(*retry_state_, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
    }
    if (retry_425_response_) {
      ON_CALL(*retry_state_, wouldRetryFromRetriableStatusCode(Http::Code::TooEarly))
          .WillByDefault(Return(true));
    }
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
  bool reject_all_hosts_ = false;
  bool retry_425_response_ = false;
};

class RouterTestBase : public testing::Test {
public:
  RouterTestBase(bool start_child_span, bool suppress_envoy_headers,
                 bool suppress_grpc_request_failure_code_stats,
                 Protobuf::RepeatedPtrField<std::string> strict_headers_to_check);

  void expectResponseTimerCreate();
  void expectPerTryTimerCreate();
  void expectPerTryIdleTimerCreate(std::chrono::milliseconds timeout);
  void expectMaxStreamDurationTimerCreate(std::chrono::milliseconds duration_msec);
  AssertionResult verifyHostUpstreamStats(uint64_t success, uint64_t error);
  void verifyMetadataMatchCriteriaFromRequest(bool route_entry_has_match);
  void verifyAttemptCountInRequestBasic(bool set_include_attempt_count_in_request,
                                        absl::optional<int> preset_count, int expected_count);
  void verifyAttemptCountInResponseBasic(bool set_include_attempt_count_in_response,
                                         absl::optional<int> preset_count, int expected_count);
  void sendRequest(bool end_stream = true);
  void enableRedirects(uint32_t max_internal_redirects = 1);
  void setNumPreviousRedirect(uint32_t num_previous_redirects);
  void setIncludeAttemptCountInRequest(bool include);
  void setIncludeAttemptCountInResponse(bool include);
  void setUpstreamMaxStreamDuration(uint32_t seconds);
  void enableHedgeOnPerTryTimeout();

  void testAppendCluster(absl::optional<Http::LowerCaseString> cluster_header_name);
  void testAppendUpstreamHost(absl::optional<Http::LowerCaseString> hostname_header_name,
                              absl::optional<Http::LowerCaseString> host_address_header_name);
  void testDoNotForward(absl::optional<Http::LowerCaseString> not_forwarded_header_name);
  void expectNewStreamWithImmediateEncoder(Http::RequestEncoder& encoder,
                                           Http::ResponseDecoder** decoder,
                                           Http::Protocol protocol);

  Event::SimulatedTimeSystem test_time_;
  std::string upstream_zone_{"to_az"};
  envoy::config::core::v3::Locality upstream_locality_;
  envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  Stats::StatNamePool pool_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  Http::ContextImpl http_context_;
  Router::ContextImpl router_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  MockShadowWriter* shadow_writer_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  FilterConfig config_;
  RouterTestFilter router_;
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};
  Event::MockTimer* per_try_idle_timeout_{};
  Event::MockTimer* max_stream_duration_timer_{};
  Network::Address::InstanceConstSharedPtr host_address_{
      Network::Utility::resolveUrl("tcp://10.0.0.5:9211")};
  NiceMock<Http::MockRequestEncoder> original_encoder_;
  NiceMock<Http::MockRequestEncoder> second_encoder_;
  NiceMock<Network::MockConnection> connection_;
  Http::ResponseDecoder* response_decoder_ = nullptr;
  Http::TestRequestHeaderMapImpl default_request_headers_;
  Http::ResponseHeaderMapPtr redirect_headers_{
      new Http::TestResponseHeaderMapImpl{{":status", "302"}, {"location", "http://www.foo.com"}}};
  NiceMock<Tracing::MockSpan> span_;
  NiceMock<StreamInfo::MockStreamInfo> upstream_stream_info_;
  std::string redirect_records_data_ = "some data";
};

} // namespace Router
} // namespace Envoy
