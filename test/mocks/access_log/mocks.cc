#include "test/mocks/access_log/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Lyft {
using testing::_;
using testing::Return;

namespace AccessLog {

MockAccessLogManager::MockAccessLogManager() {
  ON_CALL(*this, createAccessLog(_)).WillByDefault(Return(file_));
}

MockAccessLogManager::~MockAccessLogManager() {}

} // AccessLog
} // Lyft