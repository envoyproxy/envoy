#include "admin.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

MockAdmin::MockAdmin() {
  ON_CALL(*this, getConfigTracker()).WillByDefault(ReturnRef(config_tracker_));
  ON_CALL(*this, concurrency()).WillByDefault(Return(1));
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
}

MockAdmin::~MockAdmin() = default;

} // namespace Server
} // namespace Envoy
