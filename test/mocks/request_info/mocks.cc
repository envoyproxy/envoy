#include "test/mocks/request_info/mocks.h"

#include "common/network/address_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace RequestInfo {

MockRequestInfo::MockRequestInfo()
    : downstream_local_address_(new Network::Address::Ipv4Instance("127.0.0.2")),
      downstream_remote_address_(new Network::Address::Ipv4Instance("127.0.0.1")) {
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&host_));
  ON_CALL(*this, startTime()).WillByDefault(ReturnPointee(&start_time_));
  ON_CALL(*this, requestReceivedDuration()).WillByDefault(ReturnRef(request_received_duration_));
  ON_CALL(*this, responseReceivedDuration()).WillByDefault(ReturnRef(response_received_duration_));
  ON_CALL(*this, duration()).WillByDefault(ReturnPointee(&duration_));
  ON_CALL(*this, upstreamLocalAddress()).WillByDefault(ReturnRef(upstream_local_address_));
  ON_CALL(*this, downstreamLocalAddress()).WillByDefault(ReturnRef(downstream_local_address_));
  ON_CALL(*this, downstreamRemoteAddress()).WillByDefault(ReturnRef(downstream_remote_address_));
  ON_CALL(*this, protocol()).WillByDefault(ReturnRef(protocol_));
  ON_CALL(*this, responseCode()).WillByDefault(ReturnRef(response_code_));
  ON_CALL(*this, bytesReceived()).WillByDefault(ReturnPointee(&bytes_received_));
  ON_CALL(*this, bytesSent()).WillByDefault(ReturnPointee(&bytes_sent_));
}

MockRequestInfo::~MockRequestInfo() {}

} // namespace RequestInfo
} // namespace Envoy
