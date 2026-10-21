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

  MOCK_METHOD(void, dumpState, (std::ostream & os, int indent_level), (const, override));
  MOCK_METHOD(void, setUpstreamConnectionId, (uint64_t id), (override));
  MOCK_METHOD(std::optional<uint64_t>, upstreamConnectionId, (), (const, override));
  MOCK_METHOD(void, setUpstreamInterfaceName, (absl::string_view interface_name), (override));
  MOCK_METHOD(std::optional<absl::string_view>, upstreamInterfaceName, (), (const, override));
  MOCK_METHOD(void, setUpstreamSslConnection,
              (const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info), (override));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, upstreamSslConnection, (), (const, override));
  MOCK_METHOD(UpstreamTiming&, upstreamTiming, (), (override));
  MOCK_METHOD(const UpstreamTiming&, upstreamTiming, (), (const, override));
  MOCK_METHOD(void, setUpstreamLocalAddress,
              (const Network::Address::InstanceConstSharedPtr& upstream_local_address), (override));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, upstreamLocalAddress, (),
              (const, override));
  MOCK_METHOD(void, setUpstreamRemoteAddress,
              (const Network::Address::InstanceConstSharedPtr& upstream_remote_address),
              (override));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, upstreamRemoteAddress, (),
              (const, override));
  MOCK_METHOD(void, setUpstreamTransportFailureReason, (absl::string_view failure_reason),
              (override));
  MOCK_METHOD(const std::string&, upstreamTransportFailureReason, (), (const, override));
  MOCK_METHOD(void, setUpstreamDetectedCloseType, (DetectedCloseType close_type), (override));
  MOCK_METHOD(DetectedCloseType, upstreamDetectedCloseType, (), (const, override));
  MOCK_METHOD(void, setUpstreamLocalCloseReason, (absl::string_view failure_reason), (override));
  MOCK_METHOD(absl::string_view, upstreamLocalCloseReason, (), (const, override));
  MOCK_METHOD(void, setUpstreamHost, (Upstream::HostDescriptionConstSharedPtr host), (override));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, (), (const, override));
  MOCK_METHOD(const FilterStateSharedPtr&, upstreamFilterState, (), (const, override));
  MOCK_METHOD(void, setUpstreamFilterState, (const FilterStateSharedPtr& filter_state), (override));
  MOCK_METHOD(void, setUpstreamNumStreams, (uint64_t num_streams), (override));
  MOCK_METHOD(uint64_t, upstreamNumStreams, (), (const, override));
  MOCK_METHOD(void, setUpstreamProtocol, (Http::Protocol protocol), (override));
  MOCK_METHOD(std::optional<Http::Protocol>, upstreamProtocol, (), (const, override));
  MOCK_METHOD(void, addUpstreamHostAttempted, (Upstream::HostDescriptionConstSharedPtr host),
              (override));
  MOCK_METHOD(const std::vector<Upstream::HostDescriptionConstSharedPtr>&, upstreamHostsAttempted,
              (), (const, override));
  MOCK_METHOD(const std::vector<uint64_t>&, upstreamConnectionIdsAttempted, (), (const, override));

  std::vector<Upstream::HostDescriptionConstSharedPtr> upstream_hosts_attempted_;
  std::vector<uint64_t> upstream_connection_ids_attempted_;
  std::optional<uint64_t> upstream_connection_id_;
  std::optional<absl::string_view> interface_name_;
  Ssl::ConnectionInfoConstSharedPtr ssl_connection_info_;
  UpstreamTiming upstream_timing_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr upstream_remote_address_;
  std::string failure_reason_;
  DetectedCloseType upstream_detected_close_type_ = DetectedCloseType::Normal;
  std::string local_close_reason_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  FilterStateSharedPtr filter_state_;
  uint64_t num_streams_ = 0;
  std::optional<Http::Protocol> upstream_protocol_;
};

class MockStreamInfo : public StreamInfo {
public:
  MockStreamInfo();
  ~MockStreamInfo() override;

  // StreamInfo::StreamInfo
  MOCK_METHOD(void, setResponseFlag, (ResponseFlag response_flag), (override));
  MOCK_METHOD(void, setResponseCode, (uint32_t), (override));
  MOCK_METHOD(void, setResponseCodeDetails, (absl::string_view), (override));
  MOCK_METHOD(void, setConnectionTerminationDetails, (absl::string_view), (override));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(SystemTime, startTime, (), (const, override));
  MOCK_METHOD(MonotonicTime, startTimeMonotonic, (), (const, override));
  MOCK_METHOD(TimeSource&, timeSource, (), (const, override));
  MOCK_METHOD(void, setUpstreamInfo, (std::shared_ptr<UpstreamInfo>), (override));
  MOCK_METHOD(std::shared_ptr<UpstreamInfo>, upstreamInfo, (), (override));
  MOCK_METHOD(OptRef<const UpstreamInfo>, upstreamInfo, (), (const, override));
  MOCK_METHOD(void, onRequestComplete, (), (override));
  MOCK_METHOD(std::optional<std::chrono::nanoseconds>, currentDuration, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::nanoseconds>, requestComplete, (), (const, override));
  MOCK_METHOD(DownstreamTiming&, downstreamTiming, (), (override));
  MOCK_METHOD(OptRef<const DownstreamTiming>, downstreamTiming, (), (const, override));
  MOCK_METHOD(void, addBytesReceived, (uint64_t), (override));
  MOCK_METHOD(uint64_t, bytesReceived, (), (const, override));
  MOCK_METHOD(void, addBytesRetransmitted, (uint64_t), (override));
  MOCK_METHOD(uint64_t, bytesRetransmitted, (), (const, override));
  MOCK_METHOD(void, addPacketsRetransmitted, (uint64_t), (override));
  MOCK_METHOD(uint64_t, packetsRetransmitted, (), (const, override));
  MOCK_METHOD(void, addWireBytesReceived, (uint64_t));
  MOCK_METHOD(uint64_t, wireBytesReceived, (), (const));
  MOCK_METHOD(void, setVirtualClusterName, (const std::optional<std::string>& virtual_cluster_name),
              (override));
  MOCK_METHOD(const std::string&, getRouteName, (), (const, override));
  MOCK_METHOD(const std::optional<std::string>&, virtualClusterName, (), (const, override));
  MOCK_METHOD(std::optional<Http::Protocol>, protocol, (), (const, override));
  MOCK_METHOD(void, protocol, (Http::Protocol protocol), (override));
  MOCK_METHOD(std::optional<uint32_t>, responseCode, (), (const, override));
  MOCK_METHOD(const std::optional<std::string>&, responseCodeDetails, (), (const, override));
  MOCK_METHOD(const std::optional<std::string>&, connectionTerminationDetails, (),
              (const, override));
  MOCK_METHOD(void, addBytesSent, (uint64_t), (override));
  MOCK_METHOD(uint64_t, bytesSent, (), (const, override));
  MOCK_METHOD(void, addWireBytesSent, (uint64_t));
  MOCK_METHOD(uint64_t, wireBytesSent, (), (const));
  MOCK_METHOD(bool, hasResponseFlag, (ResponseFlag), (const, override));
  MOCK_METHOD(bool, hasAnyResponseFlag, (), (const, override));
  MOCK_METHOD(absl::Span<const ResponseFlag>, responseFlags, (), (const, override));
  MOCK_METHOD(uint64_t, legacyResponseFlags, (), (const, override));
  MOCK_METHOD(bool, healthCheck, (), (const, override));
  MOCK_METHOD(void, healthCheck, (bool is_health_check), (override));
  MOCK_METHOD(const Network::ConnectionInfoProvider&, downstreamAddressProvider, (),
              (const, override));
  MOCK_METHOD(OptRef<const Router::Route>, route, (), (const, override));
  MOCK_METHOD(Router::RouteConstSharedPtr, routeSharedPtr, (), (const, override));
  MOCK_METHOD(OptRef<const Router::VirtualHost>, virtualHost, (), (const, override));
  MOCK_METHOD(Router::VirtualHostConstSharedPtr, virtualHostSharedPtr, (), (const, override));
  MOCK_METHOD(void, addRouteResolutionTime, (std::chrono::nanoseconds duration), (override));
  MOCK_METHOD(std::chrono::nanoseconds, routeResolutionTime, (), (const, override));
  MOCK_METHOD(uint32_t, routeResolutionCount, (), (const, override));
  MOCK_METHOD(envoy::config::core::v3::Metadata&, dynamicMetadata, (), (override));
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, dynamicMetadata, (), (const, override));
  MOCK_METHOD(void, setDynamicMetadata, (const std::string&, const Protobuf::Struct&), (override));
  MOCK_METHOD(void, setDynamicMetadata,
              (const std::string&, const std::string&, const std::string&));
  MOCK_METHOD(void, setDynamicTypedMetadata, (const std::string&, const Protobuf::Any& value),
              (override));
  MOCK_METHOD(const FilterStateSharedPtr&, filterState, (), (override));
  MOCK_METHOD(const FilterState&, filterState, (), (const, override));
  MOCK_METHOD(void, setRequestHeaders, (const Http::RequestHeaderMap&), (override));
  MOCK_METHOD(const Http::RequestHeaderMap*, getRequestHeaders, (), (const, override));
  MOCK_METHOD(void, setUpstreamClusterInfo, (const Upstream::ClusterInfoConstSharedPtr&),
              (override));
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, upstreamClusterInfo, (), (const, override));
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, upstreamClusterInfoSharedPtr, (),
              (const, override));
  MOCK_METHOD(OptRef<const StreamIdProvider>, getStreamIdProvider, (), (const, override));
  MOCK_METHOD(void, setStreamIdProvider, (StreamIdProviderSharedPtr provider), (override));
  MOCK_METHOD(void, setTraceReason, (Tracing::Reason reason), (override));
  MOCK_METHOD(Tracing::Reason, traceReason, (), (const, override));
  MOCK_METHOD(std::optional<uint64_t>, connectionID, (), (const));
  MOCK_METHOD(void, setConnectionID, (uint64_t));
  MOCK_METHOD(void, setAttemptCount, (uint32_t), (override));
  MOCK_METHOD(std::optional<uint32_t>, attemptCount, (), (const, override));
  MOCK_METHOD(const BytesMeterSharedPtr&, getUpstreamBytesMeter, (), (const, override));
  MOCK_METHOD(const BytesMeterSharedPtr&, getDownstreamBytesMeter, (), (const, override));
  MOCK_METHOD(void, setUpstreamBytesMeter, (const BytesMeterSharedPtr&), (override));
  MOCK_METHOD(void, setDownstreamBytesMeter, (const BytesMeterSharedPtr&), (override));
  MOCK_METHOD(void, dumpState, (std::ostream & os, int indent_level), (const, override));
  MOCK_METHOD(bool, isShadow, (), (const, override));
  MOCK_METHOD(void, setDownstreamTransportFailureReason, (absl::string_view failure_reason),
              (override));
  MOCK_METHOD(absl::string_view, downstreamTransportFailureReason, (), (const, override));
  MOCK_METHOD(void, setDownstreamLocalCloseReason, (absl::string_view failure_reason), (override));
  MOCK_METHOD(absl::string_view, downstreamLocalCloseReason, (), (const, override));
  MOCK_METHOD(void, setDownstreamDetectedCloseType, (DetectedCloseType close_type), (override));
  MOCK_METHOD(DetectedCloseType, downstreamDetectedCloseType, (), (const, override));
  MOCK_METHOD(bool, shouldSchemeMatchUpstream, (), (const, override));
  MOCK_METHOD(void, setShouldSchemeMatchUpstream, (bool), (override));
  MOCK_METHOD(bool, shouldDrainConnectionUponCompletion, (), (const, override));
  MOCK_METHOD(void, setShouldDrainConnectionUponCompletion, (bool), (override));
  MOCK_METHOD(void, setParentStreamInfo, (const StreamInfo&), (override));
  MOCK_METHOD(void, clearParentStreamInfo, (), (override));
  MOCK_METHOD(OptRef<const StreamInfo>, parentStreamInfo, (), (const, override));
  MOCK_METHOD(void, addCustomFlag, (absl::string_view), (override));
  MOCK_METHOD(absl::string_view, customFlags, (), (const, override));
  MOCK_METHOD(std::optional<uint32_t>, codecStreamId, (), (const, override));
  MOCK_METHOD(void, setCodecStreamId, (std::optional<uint32_t> id), (override));

  Envoy::Event::SimulatedTimeSystem ts_;
  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;
  std::optional<std::chrono::nanoseconds> end_time_;
  std::optional<Http::Protocol> protocol_;
  std::optional<uint32_t> response_code_;
  std::optional<std::string> response_code_details_;
  std::optional<std::string> connection_termination_details_;
  Upstream::ClusterInfoConstSharedPtr upstream_cluster_info_;
  std::shared_ptr<UpstreamInfo> upstream_info_;
  absl::InlinedVector<ResponseFlag, 4> response_flags_;
  envoy::config::core::v3::Metadata metadata_;
  FilterStateSharedPtr filter_state_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
  std::shared_ptr<Network::ConnectionInfoSetterImpl> downstream_connection_info_provider_;
  BytesMeterSharedPtr upstream_bytes_meter_;
  BytesMeterSharedPtr downstream_bytes_meter_;
  Ssl::ConnectionInfoConstSharedPtr downstream_connection_info_;
  std::string route_name_;
  std::optional<uint32_t> attempt_count_;
  std::optional<std::string> virtual_cluster_name_;
  DownstreamTiming downstream_timing_;
  std::string downstream_transport_failure_reason_;
  std::string downstream_local_close_reason_;
  DetectedCloseType downstream_detected_close_type_{DetectedCloseType::Normal};
  std::string stream_flags_;
  Router::RouteConstSharedPtr route_;
  Router::VirtualHostConstSharedPtr virtual_host_;
};

} // namespace StreamInfo
} // namespace Envoy
