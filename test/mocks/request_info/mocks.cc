#include "test/mocks/request_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace RequestInfo {

MockRequestInfo::MockRequestInfo() {
  ON_CALL(*this, upstreamHost()).WillByDefault(Return(host_));
  ON_CALL(*this, startTime()).WillByDefault(Return(start_time_));
  ON_CALL(*this, requestReceivedDuration()).WillByDefault(ReturnRef(request_received_duration_));
  ON_CALL(*this, responseReceivedDuration()).WillByDefault(ReturnRef(response_received_duration_));
}

MockRequestInfo::~MockRequestInfo() {}

} // namespace RequestInfo
} // namespace Envoy
