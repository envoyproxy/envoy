#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/stream_info/stream_info.h"

#include "common/stream_info/filter_state_impl.h"

#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace StreamInfo {

class MockStreamInfo : public StreamInfo {
public:
  MockStreamInfo();
  ~MockStreamInfo() override;

  // StreamInfo::StreamInfo
  MOCK_METHOD(void, setResponseFlag, (ResponseFlag response_flag));
  MOCK_METHOD(void, setResponseCodeDetails, (absl::string_view));
  MOCK_METHOD(bool, intersectResponseFlags, (uint64_t), (const));
  MOCK_METHOD(void, onUpstreamHostSelected, (Upstream::HostDescriptionConstSharedPtr host));
  MOCK_METHOD(SystemTime, startTime, (), (const));
  MOCK_METHOD(MonotonicTime, startTimeMonotonic, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, lastDownstreamRxByteReceived, (), (const));
  MOCK_METHOD(void, onLastDownstreamRxByteReceived, ());
  MOCK_METHOD(void, setUpstreamTiming, (const UpstreamTiming&));
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, firstUpstreamTxByteSent, (), (const));
  MOCK_METHOD(void, onFirstUpstreamTxByteSent, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, lastUpstreamTxByteSent, (), (const));
  MOCK_METHOD(void, onLastUpstreamTxByteSent, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, firstUpstreamRxByteReceived, (), (const));
  MOCK_METHOD(void, onFirstUpstreamRxByteReceived, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, lastUpstreamRxByteReceived, (), (const));
  MOCK_METHOD(void, onLastUpstreamRxByteReceived, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, firstDownstreamTxByteSent, (), (const));
  MOCK_METHOD(void, onFirstDownstreamTxByteSent, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, lastDownstreamTxByteSent, (), (const));
  MOCK_METHOD(void, onLastDownstreamTxByteSent, ());
  MOCK_METHOD(void, onRequestComplete, ());
  MOCK_METHOD(absl::optional<std::chrono::nanoseconds>, requestComplete, (), (const));
  MOCK_METHOD(void, addBytesReceived, (uint64_t));
  MOCK_METHOD(uint64_t, bytesReceived, (), (const));
  MOCK_METHOD(void, setRouteName, (absl::string_view route_name));
  MOCK_METHOD(const std::string&, getRouteName, (), (const));
  MOCK_METHOD(absl::optional<Http::Protocol>, protocol, (), (const));
  MOCK_METHOD(void, protocol, (Http::Protocol protocol));
  MOCK_METHOD(absl::optional<uint32_t>, responseCode, (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, responseCodeDetails, (), (const));
  MOCK_METHOD(void, addBytesSent, (uint64_t));
  MOCK_METHOD(uint64_t, bytesSent, (), (const));
  MOCK_METHOD(bool, hasResponseFlag, (ResponseFlag), (const));
  MOCK_METHOD(bool, hasAnyResponseFlag, (), (const));
  MOCK_METHOD(uint64_t, responseFlags, (), (const));
  MOCK_METHOD(Upstream::HostDescriptionConstSharedPtr, upstreamHost, (), (const));
  MOCK_METHOD(void, setUpstreamLocalAddress, (const Network::Address::InstanceConstSharedPtr&));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, upstreamLocalAddress, (), (const));
  MOCK_METHOD(bool, healthCheck, (), (const));
  MOCK_METHOD(void, healthCheck, (bool is_health_check));
  MOCK_METHOD(void, setDownstreamLocalAddress, (const Network::Address::InstanceConstSharedPtr&));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, downstreamLocalAddress, (), (const));
  MOCK_METHOD(void, setDownstreamDirectRemoteAddress,
              (const Network::Address::InstanceConstSharedPtr&));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, downstreamDirectRemoteAddress, (),
              (const));
  MOCK_METHOD(void, setDownstreamRemoteAddress, (const Network::Address::InstanceConstSharedPtr&));
  MOCK_METHOD(const Network::Address::InstanceConstSharedPtr&, downstreamRemoteAddress, (),
              (const));
  MOCK_METHOD(void, setDownstreamSslConnection, (const Ssl::ConnectionInfoConstSharedPtr&));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, downstreamSslConnection, (), (const));
  MOCK_METHOD(void, setUpstreamSslConnection, (const Ssl::ConnectionInfoConstSharedPtr&));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, upstreamSslConnection, (), (const));
  MOCK_METHOD(const Router::RouteEntry*, routeEntry, (), (const));
  MOCK_METHOD(envoy::config::core::v3::Metadata&, dynamicMetadata, ());
  MOCK_METHOD(const envoy::config::core::v3::Metadata&, dynamicMetadata, (), (const));
  MOCK_METHOD(void, setDynamicMetadata, (const std::string&, const ProtobufWkt::Struct&));
  MOCK_METHOD(void, setDynamicMetadata,
              (const std::string&, const std::string&, const std::string&));
  MOCK_METHOD(const FilterStateSharedPtr&, filterState, ());
  MOCK_METHOD(const FilterState&, filterState, (), (const));
  MOCK_METHOD(const FilterStateSharedPtr&, upstreamFilterState, (), (const));
  MOCK_METHOD(void, setUpstreamFilterState, (const FilterStateSharedPtr&));
  MOCK_METHOD(void, setRequestedServerName, (const absl::string_view));
  MOCK_METHOD(const std::string&, requestedServerName, (), (const));
  MOCK_METHOD(void, setUpstreamTransportFailureReason, (absl::string_view));
  MOCK_METHOD(const std::string&, upstreamTransportFailureReason, (), (const));
  MOCK_METHOD(void, setRequestHeaders, (const Http::RequestHeaderMap&));
  MOCK_METHOD(const Http::RequestHeaderMap*, getRequestHeaders, (), (const));
  MOCK_METHOD(void, setUpstreamClusterInfo, (const Upstream::ClusterInfoConstSharedPtr&));
  MOCK_METHOD(absl::optional<Upstream::ClusterInfoConstSharedPtr>, upstreamClusterInfo, (),
              (const));
  MOCK_METHOD(Http::RequestIDExtensionSharedPtr, getRequestIDExtension, (), (const));
  MOCK_METHOD(void, setRequestIDExtension, (Http::RequestIDExtensionSharedPtr));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
  Envoy::Event::SimulatedTimeSystem ts_;
  SystemTime start_time_;
  MonotonicTime start_time_monotonic_;
  absl::optional<std::chrono::nanoseconds> last_downstream_rx_byte_received_;
  absl::optional<std::chrono::nanoseconds> first_upstream_tx_byte_sent_;
  absl::optional<std::chrono::nanoseconds> last_upstream_tx_byte_sent_;
  absl::optional<std::chrono::nanoseconds> first_upstream_rx_byte_received_;
  absl::optional<std::chrono::nanoseconds> last_upstream_rx_byte_received_;
  absl::optional<std::chrono::nanoseconds> first_downstream_tx_byte_sent_;
  absl::optional<std::chrono::nanoseconds> last_downstream_tx_byte_sent_;
  absl::optional<std::chrono::nanoseconds> end_time_;
  absl::optional<Http::Protocol> protocol_;
  absl::optional<uint32_t> response_code_;
  absl::optional<std::string> response_code_details_;
  uint64_t response_flags_{};
  envoy::config::core::v3::Metadata metadata_;
  FilterStateSharedPtr upstream_filter_state_;
  FilterStateSharedPtr filter_state_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_direct_remote_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  Ssl::ConnectionInfoConstSharedPtr downstream_connection_info_;
  Ssl::ConnectionInfoConstSharedPtr upstream_connection_info_;
  std::string requested_server_name_;
  std::string route_name_;
  std::string upstream_transport_failure_reason_;
};

} // namespace StreamInfo
} // namespace Envoy
