#include "test/mocks/access_log/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::_;

namespace Envoy {
namespace AccessLog {

MockAccessLogManager::MockAccessLogManager() {
  ON_CALL(*this, createAccessLog(_)).WillByDefault(Return(file_));
}

MockAccessLogManager::~MockAccessLogManager() {}

} // namespace AccessLog
} // namespace Envoy
