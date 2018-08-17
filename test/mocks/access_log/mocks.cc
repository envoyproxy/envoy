#include "test/mocks/access_log/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace AccessLog {

MockFilter::MockFilter() {}
MockFilter::~MockFilter() {}

MockAccessLogManager::MockAccessLogManager() {
  ON_CALL(*this, createAccessLog(_)).WillByDefault(Return(file_));
}

MockAccessLogManager::~MockAccessLogManager() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace AccessLog
} // namespace Envoy
