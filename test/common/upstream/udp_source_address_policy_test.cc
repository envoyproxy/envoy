#include <memory>

#include "source/common/network/address_impl.h"
#include "source/common/upstream/udp_source_address_policy.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/network/socket.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::StrictMock;

TEST(UdpSourceAddressPolicyTest, KernelSelected) {
  auto policy = UdpSourceAddressPolicy::kernelSelected();
  NiceMock<Network::MockSocket> socket;

  EXPECT_EQ(UdpSourceAddressPolicy::Mode::KernelSelected, policy.mode());
  EXPECT_TRUE(policy.shouldConnect());
  EXPECT_EQ(nullptr, policy.packetSourceIp());
  EXPECT_EQ(nullptr, policy.sourceAddress());
  EXPECT_TRUE(policy.prepareSocket(socket).ok());
}

TEST(UdpSourceAddressPolicyTest, Configured) {
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("192.0.2.1", 0);
  auto socket_option = std::make_shared<StrictMock<Network::MockSocketOption>>();
  auto socket_options = std::make_shared<Network::ConnectionSocket::Options>();
  socket_options->push_back(socket_option);
  auto policy = UdpSourceAddressPolicy::fromUpstreamLocalAddress({address, socket_options});
  NiceMock<Network::MockSocket> socket;

  EXPECT_EQ(UdpSourceAddressPolicy::Mode::Configured, policy.mode());
  EXPECT_TRUE(policy.shouldConnect());
  EXPECT_EQ(nullptr, policy.packetSourceIp());
  EXPECT_EQ(address, policy.sourceAddress());
  EXPECT_CALL(*socket_option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(true));
  EXPECT_CALL(socket, bind(address)).WillOnce(Return(Api::SysCallIntResult{0, 0}));
  EXPECT_TRUE(policy.prepareSocket(socket).ok());
}

TEST(UdpSourceAddressPolicyTest, Transparent) {
  auto downstream_address = std::make_shared<Network::Address::Ipv4Instance>("192.0.2.2", 12345);
  auto policy = UdpSourceAddressPolicy::transparent(downstream_address);

  EXPECT_EQ(UdpSourceAddressPolicy::Mode::Transparent, policy.mode());
  EXPECT_FALSE(policy.shouldConnect());
  EXPECT_EQ(downstream_address->ip(), policy.packetSourceIp());
  EXPECT_EQ(nullptr, policy.sourceAddress());
}

TEST(UdpSourceAddressPolicyTest, SocketOptionFailure) {
  auto socket_option = std::make_shared<StrictMock<Network::MockSocketOption>>();
  auto socket_options = std::make_shared<Network::ConnectionSocket::Options>();
  socket_options->push_back(socket_option);
  auto policy = UdpSourceAddressPolicy::fromUpstreamLocalAddress({nullptr, socket_options});
  NiceMock<Network::MockSocket> socket;

  EXPECT_CALL(*socket_option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(false));
  EXPECT_CALL(socket, bind(_)).Times(0);
  const auto result = policy.prepareSocket(socket);
  EXPECT_EQ(UdpSourceAddressPolicy::SocketSetupResult::FailureReason::SocketOption,
            result.failure_reason_);
}

TEST(UdpSourceAddressPolicyTest, BindFailure) {
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::Ipv4Instance>("192.0.2.1", 0);
  auto policy = UdpSourceAddressPolicy::fromUpstreamLocalAddress({address, nullptr});
  NiceMock<Network::MockSocket> socket;

  EXPECT_CALL(socket, bind(address)).WillOnce(Return(Api::SysCallIntResult{-1, 123}));
  const auto result = policy.prepareSocket(socket);
  EXPECT_EQ(UdpSourceAddressPolicy::SocketSetupResult::FailureReason::Bind, result.failure_reason_);
  EXPECT_EQ(123, result.sys_errno_);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
