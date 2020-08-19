#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/extensions/upstreams/http/http/v3/http_connection_pool.pb.h"
#include "envoy/extensions/upstreams/http/tcp/v3/tcp_connection_pool.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/config/metadata.h"
#include "common/config/well_known_names.h"
#include "common/http/context_impl.h"
#include "common/network/application_protocol.h"
#include "common/network/socket_option_factory.h"
#include "common/network/upstream_server_name.h"
#include "common/network/upstream_subject_alt_names.h"
#include "common/network/utility.h"
#include "common/router/config_impl.h"
#include "common/router/debug_config.h"
#include "common/router/router.h"
#include "common/stream_info/uint32_accessor_impl.h"
#include "common/tracing/http_tracer_impl.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/http/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::AssertionFailure;
using testing::AssertionResult;
using testing::AssertionSuccess;
using testing::AtLeast;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Matcher;
using testing::MockFunction;
using testing::NiceMock;
using testing::Property;
using testing::Return;
using testing::ReturnRef;
using testing::StartsWith;

namespace Envoy {
namespace Router {

class RouterTestFilter : public Filter {
public:
  using Filter::Filter;
  // Filter
  RetryStatePtr createRetryState(const RetryPolicy&, Http::RequestHeaderMap&,
                                 const Upstream::ClusterInfo&, const VirtualCluster*,
                                 Runtime::Loader&, Random::RandomGenerator&, Event::Dispatcher&,
                                 TimeSource&, Upstream::ResourcePriority) override {
    EXPECT_EQ(nullptr, retry_state_);
    retry_state_ = new NiceMock<MockRetryState>();
    if (reject_all_hosts_) {
      // Set up RetryState to always reject the host
      ON_CALL(*retry_state_, shouldSelectAnotherHost(_)).WillByDefault(Return(true));
    }
    return RetryStatePtr{retry_state_};
  }

  const Network::Connection* downstreamConnection() const override {
    return &downstream_connection_;
  }

  NiceMock<Network::MockConnection> downstream_connection_;
  MockRetryState* retry_state_{};
  bool reject_all_hosts_ = false;
};

class RouterTestLib {
public:
  RouterTestLib(bool start_child_span, bool suppress_envoy_headers,
                Protobuf::RepeatedPtrField<std::string> strict_headers_to_check);

  void expectResponseTimerCreate();
  void expectPerTryTimerCreate();
  void expectMaxStreamDurationTimerCreate();
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

  Event::SimulatedTimeSystem test_time_;
  std::string upstream_zone_{"to_az"};
  envoy::config::core::v3::Locality upstream_locality_;
  envoy::config::core::v3::HttpProtocolOptions common_http_protocol_options_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  Envoy::ConnectionPool::MockCancellable cancellable_;
  Http::ContextImpl http_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  MockShadowWriter* shadow_writer_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  FilterConfig config_;
  RouterTestFilter router_;
  Event::MockTimer* response_timeout_{};
  Event::MockTimer* per_try_timeout_{};
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
};

} // namespace Router
} // namespace Envoy
