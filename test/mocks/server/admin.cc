#include "admin.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

MockAdmin::MockAdmin() {
  ON_CALL(*this, getConfigTracker()).WillByDefault(ReturnRef(config_tracker_));
  ON_CALL(*this, concurrency()).WillByDefault(Return(1));
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
  ON_CALL(*this, addHandler(_, _, _, _, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, removeHandler(_)).WillByDefault(Return(true));
}

MockAdmin::~MockAdmin() = default;

} // namespace Server
} // namespace Envoy
