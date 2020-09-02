#include "common/tcp/conn_info.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/socket.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Tcp {
namespace {

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArg;

#if defined(__linux__)

TEST(ConnectionInfo, ReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Network::MockSocket> socket;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _)).WillOnce(Return(Api::SysCallIntResult{-1, -1}));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket),
              Eq(absl::optional<std::chrono::milliseconds>{}));
}

TEST(ConnectionInfo, ReturnsRttIfSuccessful) {
  NiceMock<Envoy::Network::MockSocket> socket;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _))
      .WillOnce(DoAll(WithArg<2>(Invoke([](void* optval) {
                        static_cast<struct tcp_info*>(optval)->tcpi_rtt = 35;
                      })),
                      Return(Api::SysCallIntResult{0, 0})));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket),
              Eq(absl::optional<std::chrono::milliseconds>{35}));
}

#endif

#if !defined(__linux__)

TEST(ConnectionInfo, AlwaysReturnsEmptyOptional) {
  NiceMock<Envoy::Network::MockSocket> socket;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _)).WillOnce(Return(Api::SysCallIntResult{-1, -1}));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket),
              Eq(absl::optional<std::chrono::milliseconds>{}));
}

#endif

} // namespace
} // namespace Tcp
} // namespace Envoy
