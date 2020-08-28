#include "common/tcp/conn_info.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/socket.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Tcp {
namespace {

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::DoAll;
using testing::SetArgPointee;
using testing::SetArgReferee;
using testing::NiceMock;
using testing::Property;
using testing::Return;

#if defined(__linux__)

TEST(ConnectionInfo, ReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Network::MockSocket> socket;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _)).WillOnce(Return(Api::SysCallIntResult{-1, -1})); 
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket),
              Eq(absl::optional<std::chrono::milliseconds>{}));
}

TEST(ConnectionInfo, ReturnsRttIfSuccessful) {
  NiceMock<Envoy::Network::MockSocket> socket;
  struct tcp_info ti;
  ti.tcpi_rtt = 34;
  EXPECT_CALL(socket, getSocketOption(_, _, _, _)).WillOnce(
  	DoAll(SetArgPointee<2>(ti), Return(Api::SysCallIntResult{0, 0})));
  EXPECT_THAT(ConnectionInfo::lastRoundTripTime(&socket),
              Eq(absl::optional<std::chrono::milliseconds>{34}));
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

