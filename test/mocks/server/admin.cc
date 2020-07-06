#include "admin.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
MockAdmin::MockAdmin() {
  ON_CALL(*this, getConfigTracker()).WillByDefault(testing::ReturnRef(config_tracker_));
}

MockAdmin::~MockAdmin() = default;

} // namespace Server
} // namespace Envoy
