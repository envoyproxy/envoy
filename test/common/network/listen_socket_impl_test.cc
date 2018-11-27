#include "envoy/common/exception.h"

#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::Return;

namespace Envoy {
namespace Network {

template <Network::Address::SocketType Type>
class ListenSocketImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  ListenSocketImplTest() : version_(GetParam()) {}
  const Address::IpVersion version_;

  template <typename... Args>
  std::unique_ptr<ListenSocketImpl> createListenSocketPtr(Args&&... args) {
    using NetworkSocketTraitType = NetworkSocketTrait<Type>;

    return std::make_unique<NetworkListenSocket<NetworkSocketTraitType>>(
        std::forward<Args>(args)...);
  }

  void testBindSpecificPort() {
    auto addr_fd = Network::Test::bindFreeLoopbackPort(version_, Address::SocketType::Stream);
    auto addr = addr_fd.first;
    EXPECT_LE(0, addr_fd.second);

    // Confirm that we got a reasonable address and port.
    ASSERT_EQ(Address::Type::Ip, addr->type());
    ASSERT_EQ(version_, addr->ip()->version());
    ASSERT_LT(0U, addr->ip()->port());

    // Release the socket and re-bind it.
    // WARNING: This test has a small but real risk of flaky behavior if another thread or process
    // should bind to our assigned port during the interval between closing the fd and re-binding.
    // TODO(jamessynge): Consider adding a loop or other such approach to this test so that a
    // bind failure (in the TcpListenSocket ctor) once isn't considered an error.
    EXPECT_EQ(0, close(addr_fd.second));

    auto option = std::make_unique<MockSocketOption>();
    auto options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    EXPECT_CALL(*option, setOption(_, envoy::api::v2::core::SocketOption::STATE_PREBIND))
        .WillOnce(Return(true));
    options->emplace_back(std::move(option));
    auto socket1 = createListenSocketPtr(addr, options, true);
    // TODO (conqerAtapple): This is unfortunate. We should be able to templatize this
    // instead of if block.
    if (NetworkSocketTrait<Type>::type == Address::SocketType::Stream) {
      EXPECT_EQ(0, listen(socket1->fd(), 0));
    }

    EXPECT_EQ(addr->ip()->port(), socket1->localAddress()->ip()->port());
    EXPECT_EQ(addr->ip()->addressAsString(), socket1->localAddress()->ip()->addressAsString());

    auto option2 = std::make_unique<MockSocketOption>();
    auto options2 = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
    EXPECT_CALL(*option2, setOption(_, envoy::api::v2::core::SocketOption::STATE_PREBIND))
        .WillOnce(Return(true));
    options2->emplace_back(std::move(option2));
    // The address and port are bound already, should throw exception.
    EXPECT_THROW(createListenSocketPtr(addr, options2, true), EnvoyException);

    // Test the case of a socket with fd and given address and port.
    auto socket3 = createListenSocketPtr(dup(socket1->fd()), addr, nullptr);
    EXPECT_EQ(addr->asString(), socket3->localAddress()->asString());
  }

  void testBindPortZero() {
    auto loopback = Network::Test::getCanonicalLoopbackAddress(version_);
    auto socket = createListenSocketPtr(loopback, nullptr, true);
    EXPECT_EQ(Address::Type::Ip, socket->localAddress()->type());
    EXPECT_EQ(version_, socket->localAddress()->ip()->version());
    EXPECT_EQ(loopback->ip()->addressAsString(), socket->localAddress()->ip()->addressAsString());
    EXPECT_GT(socket->localAddress()->ip()->port(), 0U);
  }
};

using ListenSocketImplTestTcp = ListenSocketImplTest<Network::Address::SocketType::Stream>;
using ListenSocketImplTestUdp = ListenSocketImplTest<Network::Address::SocketType::Datagram>;

INSTANTIATE_TEST_CASE_P(IpVersions, ListenSocketImplTestTcp,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

INSTANTIATE_TEST_CASE_P(IpVersions, ListenSocketImplTestUdp,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(ListenSocketImplTestTcp, BindSpecificPort) { testBindSpecificPort(); }

TEST_P(ListenSocketImplTestUdp, BindSpecificPort) { testBindSpecificPort(); }

// Validate that we get port allocation when binding to port zero.
TEST_P(ListenSocketImplTestTcp, BindPortZero) { testBindPortZero(); }

TEST_P(ListenSocketImplTestUdp, BindPortZero) { testBindPortZero(); }

} // namespace Network
} // namespace Envoy
