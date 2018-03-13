#pragma once

#include "envoy/request_info/request_info.h"

#include "test/mocks/upstream/host.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace RequestInfo {

class MockRequestInfo : public RequestInfo {
public:
  MockRequestInfo();
  ~MockRequestInfo();

  // RequestInfo::RequestInfo
  MOCK_METHOD1(setResponseFlag, void(ResponseFlag response_flag));
  MOCK_METHOD1(onUpstreamHostSelected, void(Upstream::HostDescriptionConstSharedPtr host));
  MOCK_CONST_METHOD0(startTime, SystemTime());
  MOCK_CONST_METHOD0(startTimeMonotonic, MonotonicTime());
  MOCK_CONST_METHOD0(lastDownstreamRxByteReceived, absl::optional<std::chrono::nanoseconds>());
  MOCK_METHOD0(onLastDownstreamRxByteReceived, void());
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
  MOCK_METHOD0(resetUpstreamTimings, void());
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_CONST_METHOD0(protocol, absl::optional<Http::Protocol>());
  MOCK_METHOD1(protocol, void(Http::Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, absl::optional<uint32_t>());
  MOCK_CONST_METHOD0(bytesSent, uint64_t());
  MOCK_CONST_METHOD1(getResponseFlag, bool(ResponseFlag));
  MOCK_CONST_METHOD0(upstreamHost, Upstream::HostDescriptionConstSharedPtr());
  MOCK_CONST_METHOD0(upstreamLocalAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(healthCheck, bool());
  MOCK_METHOD1(healthCheck, void(bool is_hc));
  MOCK_CONST_METHOD0(downstreamLocalAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(downstreamRemoteAddress, const Network::Address::InstanceConstSharedPtr&());
  MOCK_CONST_METHOD0(routeEntry, const Router::RouteEntry*());

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
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  absl::optional<Http::Protocol> protocol_;
  absl::optional<uint32_t> response_code_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
};

} // namespace RequestInfo
} // namespace Envoy
