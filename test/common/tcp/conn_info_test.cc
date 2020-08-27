#include "common/tcp/conn_info.h"

#include "test/mocks/network/mocks.h"

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
using testing::Return;

TEST(ConnectionInfo, ReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Socket::MockSocket> socket;
  EXPECT_CALL(&socket, getSocketOption(_, _, _, _)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket), Eq(absl::optional{}));
}

}
} // namespace Tcp
} // namespace Envoy
