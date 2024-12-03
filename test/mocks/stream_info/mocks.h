#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/network/socket_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/stream_info/stream_info_impl.h"

#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"

namespace testing {

template <>
class Matcher<Envoy::StreamInfo::ResponseFlag>
    : public internal::MatcherBase<Envoy::StreamInfo::ResponseFlag> {
public:
  explicit Matcher() = default;

  template <typename M, typename = typename std::remove_reference<M>::type::is_gtest_matcher>
  Matcher(M&& m) : internal::MatcherBase<Envoy::StreamInfo::ResponseFlag>(std::forward<M>(m)) {}

  Matcher(Envoy::StreamInfo::ResponseFlag value) { *this = Eq(value); }
  Matcher(Envoy::StreamInfo::CoreResponseFlag value) {
    *this = Eq(Envoy::StreamInfo::ResponseFlag(value));
  }

  explicit Matcher(const MatcherInterface<const Envoy::StreamInfo::ResponseFlag&>* impl)
      : internal::MatcherBase<Envoy::StreamInfo::ResponseFlag>(impl) {}

  template <typename U>
  explicit Matcher(const MatcherInterface<U>* impl,
                   typename std::enable_if<!std::is_same<U, const U&>::value>::type* = nullptr)
      : internal::MatcherBase<Envoy::StreamInfo::ResponseFlag>(impl) {}
};

} // namespace testing

namespace Envoy {
namespace StreamInfo {

class MockUpstreamInfo : public UpstreamInfo {
public:
  MockUpstreamInfo();
  ~MockUpstreamInfo() override;

  MOCK_METHOD(void, dumpState, (std::ostream & os, int indent_level), (const));
  MOCK_METHOD(void, setUpstreamConnectionId, (uint64_t id));
  MOCK_METHOD(absl::optional<uint64_t>, upstreamConnectionId, (), (const));
  MOCK_METHOD(void, setUpstreamInterfaceName, (absl::string_view interface_name));
  MOCK_METHOD(absl::optional<absl::string_view>, upstreamInterfaceName, (), (const));
  MOCK_METHOD(void, setUpstreamSslConnection,
              (const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, upstreamSslConnection, (), (const));
  MOCK_METHOD(UpstreamTiming&, upstreamTiming, ());
  MOCK_METHOD(const UpstreamTiming&, upstreamTiming, (), (const));
  MOCK_METHOD(void, setUpstreamLocalAddress,
              (const Network::Address::InstanceConstSharedPtr& upstream_local_address));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, upstreamLocalAddress, (), (const));
  MOCK_METHOD(void, setUpstreamRemoteAddress,
              (const Network::Address::InstanceConstSharedPtr& upstream_remote_address));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, upstreamRemoteAddress, (), (const));
  MOCK_METHOD(void, setUpstreamTransportFailureReason, (absl::string_view failure_reason));
  MOCK_METHOD(const std::string&, upstreamTransportFailureReason, (), (const));
  MOCK_METHOD(void, setUpstreamHost, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, (), (const));
  MOCK_METHOD(const FilterStateSharedPtr&, upstreamFilterState, (), (const));
  MOCK_METHOD(void, setUpstreamFilterState, (const FilterStateSharedPtr& filter_state));
  MOCK_METHOD(void, setUpstreamNumStreams, (uint64_t num_streams));
  MOCK_METHOD(uint64_t, upstreamNumStreams, (), (const));
  MOCK_METHOD(void, setUpstreamProtocol, (Http::Protocol protocol));
  MOCK_METHOD(absl::optional<Http::Protocol>, upstreamProtocol, (), (const));

  absl::optional<uint64_t> upstream_connection_id_;
  absl::optional<absl::string_view> interface_name_;
  Ssl::ConnectionInfoConstSharedPtr ssl_connection_info_;
  UpstreamTiming upstream_timing_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  std::string failure_reason_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  FilterStateSharedPtr filter_state_;
  uint64_t num_streams_ = 0;
  absl::optional<Http::Protocol> upstream_protocol_;
};

class MockStreamInfo : public StreamInfo {
public:
  MockStreamInfo();
  ~MockStreamInfo() override;

  // StreamInfo::StreamInfo
  MOCK_METHOD(void, setResponseFlag, (ResponseFlag response_flag));
  MOCK_METHOD(void, setResponseCode, (uint32_t));
  MOCK_METHOD(void, setResponseCodeDetails, (absl::string_view));
  MOCK_METHOD(void, setConnectionTerminationDetails, (absl::string_view));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(SystemTime, startTime, (), (const));
  MOCK_METHOD(MonotonicTime, startTimeMonotonic, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, (), (const));
  MOCK_METHOD(void, setUpstreamInfo, (std::shared_ptr<UpstreamInfo>));
  MOCK_METHOD(std::shared_ptr<UpstreamInfo>, upstreamInfo, ());
  MOCK_METHOD(OptRef<const UpstreamInfo>, upstreamInfo, (), (const));
  MOCK_METHOD(void, onRequestComplete, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, currentDuration, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, requestComplete, (), (const));
  MOCK_METHOD(DownstreamTiming&, downstreamTiming, ());
  MOCK_METHOD(OptRef<const DownstreamTiming>, downstreamTiming, (), (const));
  MOCK_METHOD(void, addBytesReceived, (uint64_t));
  MOCK_METHOD(uint64_t, bytesReceived, (), (const));
  MOCK_METHOD(void, addBytesRetransmitted, (uint64_t));
  MOCK_METHOD(uint64_t, bytesRetransmitted, (), (const));
  MOCK_METHOD(void, addPacketsRetransmitted, (uint64_t));
  MOCK_METHOD(uint64_t, packetsRetransmitted, (), (const));
  MOCK_METHOD(void, addWireBytesReceived, (uint64_t));
  MOCK_METHOD(uint64_t, wireBytesReceived, (), (const));
  MOCK_METHOD(void, setVirtualClusterName,
              (const absl::optional<std::string>& virtual_cluster_name));
  MOCK_METHOD(const std::string&, getRouteName, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, virtualClusterName, (), (const));
  MOCK_METHOD(absl::optional<Http::Protocol>, protocol, (), (const));
  MOCK_METHOD(void, protocol, (Http::Protocol protocol));
  MOCK_METHOD(absl::optional<uint32_t>, responseCode, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, responseCodeDetails, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, connectionTerminationDetails, (), (const));
  MOCK_METHOD(void, addBytesSent, (uint64_t));
  MOCK_METHOD(uint64_t, bytesSent, (), (const));
  MOCK_METHOD(void, addWireBytesSent, (uint64_t));
  MOCK_METHOD(uint64_t, wireBytesSent, (), (const));
  MOCK_METHOD(bool, hasResponseFlag, (ResponseFlag), (const));
  MOCK_METHOD(bool, hasAnyResponseFlag, (), (const));
  MOCK_METHOD(absl::Span<const ResponseFlag>, responseFlags, (), (const));
  MOCK_METHOD(uint64_t, legacyResponseFlags, (), (const));
  MOCK_METHOD(bool, healthCheck, (), (const));
  MOCK_METHOD(void, healthCheck, (bool is_health_check));
  MOCK_METHOD(const Network::ConnectionInfoProvider&, downstreamAddressProvider, (), (const));
  MOCK_METHOD(Router::RouteConstSharedPtr, route, (), (const));
  MOCK_METHOD(envoy::config::core::v3::Metadata&, dynamicMetadata, ());
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, dynamicMetadata, (), (const));
  MOCK_METHOD(void, setDynamicMetadata, (const std::string&, const ProtobufWkt::Struct&));
  MOCK_METHOD(void, setDynamicMetadata,
              (const std::string&, const std::string&, const std::string&));
  MOCK_METHOD(void, setDynamicTypedMetadata, (const std::string&, const ProtobufWkt::Any& value));
  MOCK_METHOD(const FilterStateSharedPtr&, filterState, ());
  MOCK_METHOD(const FilterState&, filterState, (), (const));
  MOCK_METHOD(void, setRequestHeaders, (const Http::RequestHeaderMap&));
  MOCK_METHOD(const Http::RequestHeaderMap*, getRequestHeaders, (), (const));
  MOCK_METHOD(void, setUpstreamClusterInfo, (const Upstream::ClusterInfoConstSharedPtr&));
  MOCK_METHOD(absl::optional<Upstream::ClusterInfoConstSharedPtr>, upstreamClusterInfo, (),
              (const));
  MOCK_METHOD(OptRef<const StreamIdProvider>, getStreamIdProvider, (), (const));
  MOCK_METHOD(void, setStreamIdProvider, (StreamIdProviderSharedPtr provider));
  MOCK_METHOD(void, setTraceReason, (Tracing::Reason reason));
  MOCK_METHOD(Tracing::Reason, traceReason, (), (const));
  MOCK_METHOD(absl::optional<uint64_t>, connectionID, (), (const));
  MOCK_METHOD(void, setConnectionID, (uint64_t));
  MOCK_METHOD(void, setAttemptCount, (uint32_t), ());
  MOCK_METHOD(absl::optional<uint32_t>, attemptCount, (), (const));
  MOCK_METHOD(const BytesMeterSharedPtr&, getUpstreamBytesMeter, (), (const));
  MOCK_METHOD(const BytesMeterSharedPtr&, getDownstreamBytesMeter, (), (const));
  MOCK_METHOD(void, setUpstreamBytesMeter, (const BytesMeterSharedPtr&));
  MOCK_METHOD(void, setDownstreamBytesMeter, (const BytesMeterSharedPtr&));
  MOCK_METHOD(void, dumpState, (std::ostream & os, int indent_level), (const));
  MOCK_METHOD(bool, isShadow, (), (const, override));
  MOCK_METHOD(void, setDownstreamTransportFailureReason, (absl::string_view failure_reason));
  MOCK_METHOD(absl::string_view, downstreamTransportFailureReason, (), (const));
  MOCK_METHOD(bool, shouldSchemeMatchUpstream, (), (const));
  MOCK_METHOD(void, setShouldSchemeMatchUpstream, (bool));
  MOCK_METHOD(bool, shouldDrainConnectionUponCompletion, (), (const));
  MOCK_METHOD(void, setShouldDrainConnectionUponCompletion, (bool));
  MOCK_METHOD(void, setParentStreamInfo, (const StreamInfo&), ());
  MOCK_METHOD(void, clearParentStreamInfo, ());
  MOCK_METHOD(OptRef<const StreamInfo>, parentStreamInfo, (), (const));

  Envoy::Event::SimulatedTimeSystem ts_;
  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;
  absl::optional<std::chrono::nanoseconds> end_time_;
  absl::optional<Http::Protocol> protocol_;
  absl::optional<uint32_t> response_code_;
  absl::optional<std::string> response_code_details_;
  absl::optional<std::string> connection_termination_details_;
  absl::optional<Upstream::ClusterInfoConstSharedPtr> upstream_cluster_info_;
  std::shared_ptr<UpstreamInfo> upstream_info_;
  absl::InlinedVector<ResponseFlag, 4> response_flags_{};
  envoy::config::core::v3::Metadata metadata_;
  FilterStateSharedPtr filter_state_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
  std::shared_ptr<Network::ConnectionInfoSetterImpl> downstream_connection_info_provider_;
  BytesMeterSharedPtr upstream_bytes_meter_;
  BytesMeterSharedPtr downstream_bytes_meter_;
  Ssl::ConnectionInfoConstSharedPtr downstream_connection_info_;
  std::string route_name_;
  absl::optional<uint32_t> attempt_count_;
  absl::optional<std::string> virtual_cluster_name_;
  DownstreamTiming downstream_timing_;
  std::string downstream_transport_failure_reason_;
};

} // namespace StreamInfo
} // namespace Envoy
