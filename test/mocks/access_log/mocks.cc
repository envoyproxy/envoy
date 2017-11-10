#include "test/mocks/access_log/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace AccessLog {

MockAccessLogManager::MockAccessLogManager() {
  ON_CALL(*this, createAccessLog(_)).WillByDefault(Return(file_));
}

MockAccessLogManager::~MockAccessLogManager() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

MockRequestInfo::MockRequestInfo() {
  ON_CALL(*this, upstreamHost()).WillByDefault(Return(host_));
  ON_CALL(*this, startTime()).WillByDefault(Return(start_time_));
  ON_CALL(*this, requestReceivedDuration()).WillByDefault(ReturnRef(request_received_duration_));
  ON_CALL(*this, responseReceivedDuration()).WillByDefault(ReturnRef(response_received_duration_));
}

MockRequestInfo::~MockRequestInfo() {}

} // namespace AccessLog
} // namespace Envoy
