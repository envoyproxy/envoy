#pragma once

#include "envoy/stream_info/stream_info.h"

#include "common/stream_info/filter_state_impl.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace StreamInfo {

class MockStreamInfo : public StreamInfo {
public:
  MockStreamInfo();
  ~MockStreamInfo();

  // StreamInfo::StreamInfo
  MOCK_METHOD1(setResponseFlag, void(ResponseFlag response_flag));
  MOCK_METHOD1(setResponseCodeDetails, void(absl::string_view));
  MOCK_CONST_METHOD1(intersectResponseFlags, bool(uint64_t));
  MOCK_METHOD1(onUpstreamHostSelected, void(Upstream::HostDescriptionConstSharedPtr host));
  MOCK_CONST_METHOD0(startTime, SystemTime());
  MOCK_CONST_METHOD0(startTimeMonotonic, MonotonicTime());
  MOCK_CONST_METHOD0(lastDownstreamRxByteReceived, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onLastDownstreamRxByteReceived, void());
  MOCK_METHOD1(setUpstreamTiming, void(const UpstreamTiming&));
  MOCK_CONST_METHOD0(firstUpstreamTxByteSent, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onFirstUpstreamTxByteSent, void());
  MOCK_CONST_METHOD0(lastUpstreamTxByteSent, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onLastUpstreamTxByteSent, void());
  MOCK_CONST_METHOD0(firstUpstreamRxByteReceived, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onFirstUpstreamRxByteReceived, void());
  MOCK_CONST_METHOD0(lastUpstreamRxByteReceived, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onLastUpstreamRxByteReceived, void());
  MOCK_CONST_METHOD0(firstDownstreamTxByteSent, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onFirstDownstreamTxByteSent, void());
  MOCK_CONST_METHOD0(lastDownstreamTxByteSent, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onLastDownstreamTxByteSent, void());
  MOCK_METHOD0(onRequestComplete, void());
  MOCK_CONST_METHOD0(requestComplete, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD1(addBytesReceived, void(uint64_t));
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_METHOD1(setRouteName, void(absl::string_view route_name));
  MOCK_CONST_METHOD0(getRouteName, const std::string&());
  MOCK_CONST_METHOD0(protocol, absl::optional<Http::Protocol>());
  MOCK_METHOD1(protocol, void(Http::Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, absl::optional<uint32_t>());
  MOCK_CONST_METHOD0(responseCodeDetails, const absl::optional<std::string>&());
  MOCK_METHOD1(addBytesSent, void(uint64_t));
  MOCK_CONST_METHOD0(bytesSent, uint64_t());
  MOCK_CONST_METHOD1(hasResponseFlag, bool(ResponseFlag));
  MOCK_CONST_METHOD0(hasAnyResponseFlag, bool());
  MOCK_CONST_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_METHOD1(setUpstreamLocalAddress, void(const Network::Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(upstreamLocalAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(healthCheck, bool());
  MOCK_METHOD1(healthCheck, void(bool is_health_check));
  MOCK_METHOD1(setDownstreamLocalAddress, void(const Network::Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(downstreamLocalAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setDownstreamDirectRemoteAddress,
               void(const Network::Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(downstreamDirectRemoteAddress,
                     const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setDownstreamRemoteAddress, void(const Network::Address::InstanceConstSharedPtr&));
  MOCK_CONST_METHOD0(downstreamRemoteAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_METHOD1(setDownstreamSslConnection, void(const Ssl::ConnectionInfo*));
  MOCK_CONST_METHOD0(downstreamSslConnection, const Ssl::ConnectionInfo*());
  MOCK_CONST_METHOD0(routeEntry, const Router::RouteEntry*());
  MOCK_METHOD0(dynamicMetadata, envoy::api::v2::core::Metadata&());
  MOCK_CONST_METHOD0(dynamicMetadata, const envoy::api::v2::core::Metadata&());
  MOCK_METHOD2(setDynamicMetadata, void(const std::string&, const ProtobufWkt::Struct&));
  MOCK_METHOD3(setDynamicMetadata,
               void(const std::string&, const std::string&, const std::string&));
  MOCK_METHOD0(filterState, FilterState&());
  MOCK_CONST_METHOD0(filterState, const FilterState&());
  MOCK_METHOD1(setRequestedServerName, void(const absl::string_view));
  MOCK_CONST_METHOD0(requestedServerName, const std::string&());
  MOCK_METHOD1(setUpstreamTransportFailureReason, void(absl::string_view));
  MOCK_CONST_METHOD0(upstreamTransportFailureReason, const std::string&());

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
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
  envoy::api::v2::core::Metadata metadata_;
  FilterStateImpl filter_state_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_direct_remote_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  const Ssl::ConnectionInfo* downstream_connection_info_{};
  std::string requested_server_name_;
  std::string route_name_;
  std::string upstream_transport_failure_reason_;
};

} // namespace StreamInfo
} // namespace Envoy
