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
  MOCK_CONST_METHOD0(startTime, const SystemTime&());
  MOCK_CONST_METHOD0(startTimeMonotonic, MonotonicTime&());
  MOCK_CONST_METHOD0(lastDownstreamRxByteReceived, const Optional<MonotonicTime>&());
  MOCK_METHOD1(lastDownstreamRxByteReceived, void(MonotonicTime time));
  MOCK_CONST_METHOD0(firstUpstreamTxByteSent, const Optional<MonotonicTime>&());
  MOCK_METHOD1(firstUpstreamTxByteSent, void(MonotonicTime time));
  MOCK_CONST_METHOD0(lastUpstreamTxByteSent, const Optional<MonotonicTime>&());
  MOCK_METHOD1(lastUpstreamTxByteSent, void(MonotonicTime time));
  MOCK_CONST_METHOD0(firstUpstreamRxByteReceived, const Optional<MonotonicTime>&());
  MOCK_METHOD1(firstUpstreamRxByteReceived, void(MonotonicTime time));
  MOCK_CONST_METHOD0(lastUpstreamRxByteReceived, const Optional<MonotonicTime>&());
  MOCK_METHOD1(lastUpstreamRxByteReceived, void(MonotonicTime time));
  MOCK_CONST_METHOD0(firstDownstreamTxByteSent, const Optional<MonotonicTime>&());
  MOCK_METHOD1(firstDownstreamTxByteSent, void(MonotonicTime time));
  MOCK_CONST_METHOD0(lastDownstreamTxByteSent, const Optional<MonotonicTime>&());
  MOCK_METHOD1(lastDownstreamTxByteSent, void(MonotonicTime time));
  MOCK_METHOD1(finalize, void(MonotonicTime time));
  MOCK_CONST_METHOD0(finalTimeMonotonic, const Optional<MonotonicTime>&());
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_CONST_METHOD0(protocol, const Optional<Http::Protocol>&());
  MOCK_METHOD1(protocol, void(Http::Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, Optional<uint32_t>&());
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
  Optional<MonotonicTime> last_rx_byte_received_;
  Optional<MonotonicTime> first_upstream_tx_byte_sent_;
  Optional<MonotonicTime> last_upstream_tx_byte_sent_;
  Optional<MonotonicTime> first_upstream_rx_byte_received_;
  Optional<MonotonicTime> last_upstream_rx_byte_received_;
  Optional<MonotonicTime> first_downstream_tx_byte_sent_;
  Optional<MonotonicTime> last_downstream_tx_byte_sent_;
  MonotonicTime end_time_;
  Network::Address::InstanceConstSharedPtr upstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_local_address_;
  Network::Address::InstanceConstSharedPtr downstream_remote_address_;
  Optional<Http::Protocol> protocol_;
  Optional<uint32_t> response_code_;
  uint64_t bytes_received_{};
  uint64_t bytes_sent_{};
};

} // namespace RequestInfo
} // namespace Envoy
