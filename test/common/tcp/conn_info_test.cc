#include "common/tcp/conn_info.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/socket.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Tcp {
namespace {

using testing::_;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::NiceMock;
using testing::Property;
using testing::Eq;
using testing::Return;

TEST(ConnectionInfo, ReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Network::MockSocket> socket;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket), Eq(absl::optional<std::chrono::milliseconds>{}));
}

}
} // namespace Tcp
} // namespace Envoy
