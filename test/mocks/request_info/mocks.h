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
  MOCK_CONST_METHOD0(requestReceivedDuration, const Optional<std::chrono::microseconds>&());
  MOCK_METHOD1(requestReceivedDuration, void(MonotonicTime time));
  MOCK_CONST_METHOD0(responseReceivedDuration, const Optional<std::chrono::microseconds>&());
  MOCK_METHOD1(responseReceivedDuration, void(MonotonicTime time));
  MOCK_CONST_METHOD0(bytesReceived, uint64_t());
  MOCK_CONST_METHOD0(protocol, const Optional<Http::Protocol>&());
  MOCK_METHOD1(protocol, void(Http::Protocol protocol));
  MOCK_CONST_METHOD0(responseCode, Optional<uint32_t>&());
  MOCK_CONST_METHOD0(bytesSent, uint64_t());
  MOCK_CONST_METHOD0(duration, std::chrono::microseconds());
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
  Optional<std::chrono::microseconds> request_received_duration_;
  Optional<std::chrono::microseconds> response_received_duration_;
  std::chrono::microseconds duration_{};
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
