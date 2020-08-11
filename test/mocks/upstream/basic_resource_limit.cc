#include "basic_resource_limit.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

using ::testing::Return;
MockBasicResourceLimit::MockBasicResourceLimit() {
  ON_CALL(*this, canCreate()).WillByDefault(Return(true));
}

MockBasicResourceLimit::~MockBasicResourceLimit() = default;

} // namespace Upstream
} // namespace Envoy
