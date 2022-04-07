#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/config/core/v3/address.pb.h"

#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/utility.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Network {
namespace {

TEST(NetworkUtility, Url) {
  EXPECT_EQ("foo", Utility::hostFromTcpUrl("tcp://foo:1234"));
  EXPECT_EQ(1234U, Utility::portFromTcpUrl("tcp://foo:1234"));
  EXPECT_THROW(Utility::hostFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo:bar"), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://https://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromTcpUrl(""), EnvoyException);
  EXPECT_THROW(Utility::portFromTcpUrl("tcp://foo:999999999999"), EnvoyException);
}

TEST(NetworkUtility, udpUrl) {
  EXPECT_EQ("foo", Utility::hostFromUdpUrl("udp://foo:1234"));
  EXPECT_EQ(1234U, Utility::portFromUdpUrl("udp://foo:1234"));
  EXPECT_THROW(Utility::hostFromUdpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromUdpUrl("bogus://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromUdpUrl("tcp://foo"), EnvoyException);
  EXPECT_THROW(Utility::portFromUdpUrl("tcp://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::portFromUdpUrl("udp://https://foo:1234"), EnvoyException);
  EXPECT_THROW(Utility::hostFromUdpUrl(""), EnvoyException);
  EXPECT_THROW(Utility::portFromUdpUrl("udp://foo:999999999999"), EnvoyException);
}

TEST(NetworkUtility, resolveUrl) {
  EXPECT_THROW(Utility::resolveUrl("foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("abc://foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://1.2.3.4:1234/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:8001/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:0/foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://127.0.0.1:"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3.3"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3.3.3:0"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://192.168.3:0"), EnvoyException);

  EXPECT_THROW(Utility::resolveUrl("udp://1.2.3.4:1234/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://127.0.0.1:8001/"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://127.0.0.1:0/foo"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://127.0.0.1:"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://192.168.3.3"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://192.168.3.3.3:0"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://192.168.3:0"), EnvoyException);

  EXPECT_THROW(Utility::resolveUrl("tcp://[::1]"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://[:::1]:1"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("tcp://foo:0"), EnvoyException);

  EXPECT_THROW(Utility::resolveUrl("udp://[::1]"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://[:::1]:1"), EnvoyException);
  EXPECT_THROW(Utility::resolveUrl("udp://foo:0"), EnvoyException);

  EXPECT_EQ("", Utility::resolveUrl("unix://")->asString());
  EXPECT_EQ("foo", Utility::resolveUrl("unix://foo")->asString());
  EXPECT_EQ("tmp", Utility::resolveUrl("unix://tmp")->asString());
  EXPECT_EQ("tmp/server", Utility::resolveUrl("unix://tmp/server")->asString());

  EXPECT_EQ("1.2.3.4:1234", Utility::resolveUrl("tcp://1.2.3.4:1234")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::resolveUrl("tcp://0.0.0.0:0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::resolveUrl("tcp://127.0.0.1:0")->asString());

  EXPECT_EQ("[::1]:1", Utility::resolveUrl("tcp://[::1]:1")->asString());
  EXPECT_EQ("[::]:0", Utility::resolveUrl("tcp://[::]:0")->asString());
  EXPECT_EQ("[1::2:3]:4", Utility::resolveUrl("tcp://[1::2:3]:4")->asString());
  EXPECT_EQ("[a::1]:0", Utility::resolveUrl("tcp://[a::1]:0")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::resolveUrl("tcp://[a:b:c:d::]:0")->asString());

  EXPECT_EQ("1.2.3.4:1234", Utility::resolveUrl("udp://1.2.3.4:1234")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::resolveUrl("udp://0.0.0.0:0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::resolveUrl("udp://127.0.0.1:0")->asString());

  EXPECT_EQ("[::1]:1", Utility::resolveUrl("udp://[::1]:1")->asString());
  EXPECT_EQ("[::]:0", Utility::resolveUrl("udp://[::]:0")->asString());
  EXPECT_EQ("[1::2:3]:4", Utility::resolveUrl("udp://[1::2:3]:4")->asString());
  EXPECT_EQ("[a::1]:0", Utility::resolveUrl("udp://[a::1]:0")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::resolveUrl("udp://[a:b:c:d::]:0")->asString());
}

TEST(NetworkUtility, socketTypeFromUrl) {
  EXPECT_FALSE(Utility::socketTypeFromUrl("foo").ok());
  EXPECT_FALSE(Utility::socketTypeFromUrl("abc://foo").ok());

  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://foo"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("unix://tmp/server"));

  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://1.2.3.4:1234"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://0.0.0.0:0"));
  EXPECT_EQ(Network::Socket::Type::Stream, *Utility::socketTypeFromUrl("tcp://[::1]:1"));

  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://1.2.3.4:1234"));
  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://0.0.0.0:0"));
  EXPECT_EQ(Network::Socket::Type::Datagram, *Utility::socketTypeFromUrl("udp://[::1]:1"));
}

TEST(NetworkUtility, ParseInternetAddress) {
  EXPECT_THROW(Utility::parseInternetAddress(""), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3.4.5"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("1.2.3.256"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("foo"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("0:0:0:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("fffff::"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("/foo"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("[::]"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddress("[::1]:1"), EnvoyException);

  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow(""));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3.4.5"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("1.2.3.256"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("foo"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("0:0:0:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("fffff::"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("/foo"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("[::]"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressNoThrow("[::1]:1"));

  EXPECT_EQ("1.2.3.4:0", Utility::parseInternetAddress("1.2.3.4")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddress("0.0.0.0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddress("127.0.0.1")->asString());

  EXPECT_EQ("1.2.3.4:0", Utility::parseInternetAddressNoThrow("1.2.3.4")->asString());
  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressNoThrow("0.0.0.0")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressNoThrow("127.0.0.1")->asString());

  EXPECT_EQ("[::1]:0", Utility::parseInternetAddress("::1")->asString());
  EXPECT_EQ("[::]:0", Utility::parseInternetAddress("::")->asString());
  EXPECT_EQ("[1::2:3]:0", Utility::parseInternetAddress("1::2:3")->asString());
  EXPECT_EQ("[a::1]:0", Utility::parseInternetAddress("a::1")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::parseInternetAddress("a:b:c:d::")->asString());

  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressNoThrow("::1")->asString());
  EXPECT_EQ("[::]:0", Utility::parseInternetAddressNoThrow("::")->asString());
  EXPECT_EQ("[1::2:3]:0", Utility::parseInternetAddressNoThrow("1::2:3")->asString());
  EXPECT_EQ("[a::1]:0", Utility::parseInternetAddressNoThrow("a::1")->asString());
  EXPECT_EQ("[a:b:c:d::]:0", Utility::parseInternetAddressNoThrow("a:b:c:d::")->asString());
}

TEST(NetworkUtility, ParseInternetAddressAndPort) {
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4::1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:-1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort(":1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort(" :1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3:1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:65536"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("1.2.3.4:8008/"), EnvoyException);

  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4::1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:-1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(":1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(" :1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3:1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:65536"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("1.2.3.4:8008/"));

  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressAndPort("0.0.0.0:0")->asString());
  EXPECT_EQ("255.255.255.255:65535",
            Utility::parseInternetAddressAndPort("255.255.255.255:65535")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressAndPort("127.0.0.1:0")->asString());

  EXPECT_EQ("0.0.0.0:0", Utility::parseInternetAddressAndPortNoThrow("0.0.0.0:0")->asString());
  EXPECT_EQ("255.255.255.255:65535",
            Utility::parseInternetAddressAndPortNoThrow("255.255.255.255:65535")->asString());
  EXPECT_EQ("127.0.0.1:0", Utility::parseInternetAddressAndPortNoThrow("127.0.0.1:0")->asString());

  EXPECT_THROW(Utility::parseInternetAddressAndPort(""), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("::1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("::"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[[::]]:1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:1]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("]:[::1]:2"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1.2.3.4:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1.2.3.4]:0"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:-1"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[::]:bogus"), EnvoyException);
  EXPECT_THROW(Utility::parseInternetAddressAndPort("[1::1]:65536"), EnvoyException);

  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow(""));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("::1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("::"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[[::]]:1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:1]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("]:[::1]:2"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1.2.3.4:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1.2.3.4]:0"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:-1"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[::]:bogus"));
  EXPECT_EQ(nullptr, Utility::parseInternetAddressAndPortNoThrow("[1::1]:65536"));

  EXPECT_EQ("[::]:0", Utility::parseInternetAddressAndPort("[::]:0")->asString());
  EXPECT_EQ("[1::1]:65535", Utility::parseInternetAddressAndPort("[1::1]:65535")->asString());
  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressAndPort("[::1]:0")->asString());

  EXPECT_EQ("[::]:0", Utility::parseInternetAddressAndPortNoThrow("[::]:0")->asString());
  EXPECT_EQ("[1::1]:65535",
            Utility::parseInternetAddressAndPortNoThrow("[1::1]:65535")->asString());
  EXPECT_EQ("[::1]:0", Utility::parseInternetAddressAndPortNoThrow("[::1]:0")->asString());
}

class NetworkUtilityGetLocalAddress : public testing::TestWithParam<Address::IpVersion> {};

INSTANTIATE_TEST_SUITE_P(IpVersions, NetworkUtilityGetLocalAddress,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(NetworkUtilityGetLocalAddress, GetLocalAddress) {
  auto ip_version = GetParam();
  auto local_address = Utility::getLocalAddress(ip_version);
  EXPECT_NE(nullptr, local_address);
  EXPECT_EQ(ip_version, local_address->ip()->version());
}

TEST(NetworkUtility, GetOriginalDst) {
  testing::NiceMock<Network::MockConnectionSocket> socket;
#ifdef SOL_IP
  EXPECT_CALL(socket, ipVersion()).WillOnce(testing::Return(absl::nullopt));
#endif
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));

#ifdef SOL_IP
  EXPECT_CALL(socket, addressType()).WillOnce(testing::Return(Address::Type::Pipe));
#endif
  EXPECT_EQ(nullptr, Utility::getOriginalDst(socket));
}

TEST(NetworkUtility, LocalConnection) {
  testing::NiceMock<Network::MockConnectionSocket> socket;

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::PipeInstance>("/pipe/path"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.2"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("8.8.8.8"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4", 1234));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("4.4.4.4", 4321));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2"));
  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::1"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::3"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2"));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2", 4321));
  socket.connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv6Instance>("::2", 1234));
  EXPECT_TRUE(Utility::isSameIpOrLoopback(socket));

  socket.connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv6Instance>("fd00::"));
  EXPECT_FALSE(Utility::isSameIpOrLoopback(socket));
}

TEST(NetworkUtility, InternalAddress) {
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("127.0.0.1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("10.0.0.1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("192.168.0.0")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("172.16.0.0")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv4Instance("172.30.2.1")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("192.167.0.0")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("172.32.0.0")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv4Instance("11.0.0.1")));

  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fd00::")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("::1")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fdff::")));
  EXPECT_TRUE(Utility::isInternalAddress(Address::Ipv6Instance("fd01::")));
  EXPECT_TRUE(
      Utility::isInternalAddress(Address::Ipv6Instance("fd12:3456:7890:1234:5678:9012:3456:7890")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fd::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fc00::")));
  EXPECT_FALSE(Utility::isInternalAddress(Address::Ipv6Instance("fe00::")));

  EXPECT_FALSE(Utility::isInternalAddress(Address::PipeInstance("/hello")));
}

TEST(NetworkUtility, LoopbackAddress) {
  {
    Address::Ipv4Instance address("127.0.0.1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv4Instance address("10.0.0.1");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    Address::PipeInstance address("/foo");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv6Instance address("::1");
    EXPECT_TRUE(Utility::isLoopbackAddress(address));
  }
  {
    Address::Ipv6Instance address("::");
    EXPECT_FALSE(Utility::isLoopbackAddress(address));
  }
  EXPECT_EQ("127.0.0.1:0", Utility::getCanonicalIpv4LoopbackAddress()->asString());
  EXPECT_EQ("[::1]:0", Utility::getIpv6LoopbackAddress()->asString());
}

TEST(NetworkUtility, AnyAddress) {
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv4AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v4);
    EXPECT_EQ(any->asString(), "0.0.0.0:0");
    EXPECT_EQ(any, Utility::getIpv4AnyAddress());
  }
  {
    Address::InstanceConstSharedPtr any = Utility::getIpv6AnyAddress();
    ASSERT_NE(any, nullptr);
    EXPECT_EQ(any->type(), Address::Type::Ip);
    EXPECT_EQ(any->ip()->version(), Address::IpVersion::v6);
    EXPECT_EQ(any->asString(), "[::]:0");
    EXPECT_EQ(any, Utility::getIpv6AnyAddress());
  }
}

TEST(NetworkUtility, ParseProtobufAddress) {
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_address("127.0.0.1");
    proto_address.mutable_socket_address()->set_port_value(1234);
    EXPECT_EQ("127.0.0.1:1234", Utility::protobufAddressToAddress(proto_address)->asString());
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_address("::1");
    proto_address.mutable_socket_address()->set_port_value(1234);
    EXPECT_EQ("[::1]:1234", Utility::protobufAddressToAddress(proto_address)->asString());
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe()->set_path("/tmp/unix-socket");
    EXPECT_EQ("/tmp/unix-socket", Utility::protobufAddressToAddress(proto_address)->asString());
  }
#if defined(__linux__)
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe()->set_path("@/tmp/abstract-unix-socket");
    EXPECT_EQ("@/tmp/abstract-unix-socket",
              Utility::protobufAddressToAddress(proto_address)->asString());
  }
#endif
}

TEST(NetworkUtility, AddressToProtobufAddress) {
  {
    envoy::config::core::v3::Address proto_address;
    Address::Ipv4Instance address("127.0.0.1");
    Utility::addressToProtobufAddress(address, proto_address);
    EXPECT_EQ(true, proto_address.has_socket_address());
    EXPECT_EQ("127.0.0.1", proto_address.socket_address().address());
    EXPECT_EQ(0, proto_address.socket_address().port_value());
  }
  {
    envoy::config::core::v3::Address proto_address;
    Address::PipeInstance address("/hello");
    Utility::addressToProtobufAddress(address, proto_address);
    EXPECT_EQ(true, proto_address.has_pipe());
    EXPECT_EQ("/hello", proto_address.pipe().path());
  }
}

TEST(NetworkUtility, ProtobufAddressSocketType) {
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address();
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_protocol(
        envoy::config::core::v3::SocketAddress::TCP);
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_socket_address()->set_protocol(
        envoy::config::core::v3::SocketAddress::UDP);
    EXPECT_EQ(Socket::Type::Datagram, Utility::protobufAddressSocketType(proto_address));
  }
  {
    envoy::config::core::v3::Address proto_address;
    proto_address.mutable_pipe();
    EXPECT_EQ(Socket::Type::Stream, Utility::protobufAddressSocketType(proto_address));
  }
}

TEST(PortRangeListTest, Errors) {
  {
    std::string port_range_str = "a1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1A";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1_1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1X1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }

  {
    std::string port_range_str = "1,1*1";
    std::list<PortRange> port_range_list;
    EXPECT_THROW(Utility::parsePortRangeList(port_range_str, port_range_list), EnvoyException);
  }
}

static Address::Ipv4Instance makeFromPort(uint32_t port) {
  return Address::Ipv4Instance("0.0.0.0", port);
}

TEST(PortRangeListTest, Normal) {
  {
    std::string port_range_str = "1";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(Address::PipeInstance("/foo"), port_range_list));
  }

  {
    std::string port_range_str = "1024-2048";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1024), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(2048), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1536), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(1023), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2049), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(0), port_range_list));
  }

  {
    std::string port_range_str = "1,10-100,1000-10000,65535";
    std::list<PortRange> port_range_list;

    Utility::parsePortRangeList(port_range_str, port_range_list);
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(1), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(50), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(5000), port_range_list));
    EXPECT_TRUE(Utility::portInRangeList(makeFromPort(65535), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(2), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(200), port_range_list));
    EXPECT_FALSE(Utility::portInRangeList(makeFromPort(20000), port_range_list));
  }
}

// TODO(ccaraman): Support big-endian. These tests operate under the assumption that the machine
// byte order is little-endian.
TEST(AbslUint128, TestByteOrder) {
  {
    Address::Ipv6Instance address("::1");
    uint64_t high = 0x100000000000000;
    EXPECT_EQ(absl::MakeUint128(high, 0), address.ip()->ipv6()->address());
    EXPECT_EQ(absl::MakeUint128(high, 0),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));

    EXPECT_EQ(absl::uint128(1), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    Address::Ipv6Instance address("1::");
    EXPECT_EQ(absl::uint128(256), address.ip()->ipv6()->address());
    EXPECT_EQ(absl::uint128(256),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));

    uint64_t high = 0x001000000000000;
    EXPECT_EQ(absl::MakeUint128(high, 0), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    Address::Ipv6Instance address("2001:abcd:ef01:2345:6789:abcd:ef01:234");
    uint64_t low = 0x452301EFCDAB0120;
    uint64_t high = 0x340201EFCDAB8967;
    EXPECT_EQ(absl::MakeUint128(high, low), address.ip()->ipv6()->address());
    EXPECT_EQ(absl::MakeUint128(high, low),
              Utility::Ip6htonl(Utility::Ip6ntohl(address.ip()->ipv6()->address())));
  }
  {
    Address::Ipv6Instance address("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    EXPECT_EQ(absl::Uint128Max(), address.ip()->ipv6()->address());
    EXPECT_EQ(absl::Uint128Max(), Utility::Ip6ntohl(address.ip()->ipv6()->address()));
  }
  {
    TestRandomGenerator rand;
    absl::uint128 random_number = absl::MakeUint128(rand.random(), rand.random());
    EXPECT_EQ(random_number, Utility::Ip6htonl(Utility::Ip6ntohl(random_number)));
  }
}

TEST(ResolvedUdpSocketConfig, Warning) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, supportsUdpGro()).WillOnce(Return(false));
  EXPECT_LOG_CONTAINS(
      "warn", "GRO requested but not supported by the OS. Check OS config or disable prefer_gro.",
      ResolvedUdpSocketConfig resolved_config(envoy::config::core::v3::UdpSocketConfig(), true));
}

#ifndef WIN32
TEST(PacketLoss, LossTest) {
  // Create and bind a UDP socket.
  auto version = TestEnvironment::getIpVersionsForTest()[0];
  auto kernel_version = version == Network::Address::IpVersion::v4 ? AF_INET : AF_INET6;
  int fd = socket(kernel_version, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP);
  ASSERT_NE(fd, 0);
  sockaddr_storage storage;
  auto& sin = reinterpret_cast<sockaddr_in&>(storage);
  sin.sin_family = kernel_version;
  sin.sin_port = 0;
  EXPECT_EQ(1,
            inet_pton(kernel_version, Network::Test::getLoopbackAddressUrlString(version).c_str(),
                      &sin.sin_addr));
  ASSERT_EQ(0, bind(fd, reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Get the port.
  socklen_t storage_len = sizeof(storage);
  ASSERT_EQ(0, getsockname(fd, reinterpret_cast<sockaddr*>(&storage), &storage_len));

  // Set the buffer size artificially small.
  int receive_buffer_size = 1000;
  ASSERT_EQ(
      0, setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size, sizeof(receive_buffer_size)));

  // Send a packet.
  char buf[2048];
  memset(buf, 0, ABSL_ARRAYSIZE(buf));
  EXPECT_EQ(ABSL_ARRAYSIZE(buf), sendto(fd, buf, ABSL_ARRAYSIZE(buf), 0,
                                        reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Verify the packet is dropped.
  IoSocketHandleImpl handle(fd);
  auto address = Network::Test::getCanonicalLoopbackAddress(version);
  NiceMock<MockUdpPacketProcessor> processor;
  MonotonicTime time(std::chrono::seconds(0));
  uint32_t packets_dropped = 0;
  Utility::readFromSocket(handle, *address, processor, time, false, &packets_dropped);
  EXPECT_EQ(1, packets_dropped);

  // Send another packet.
  EXPECT_EQ(ABSL_ARRAYSIZE(buf), sendto(fd, buf, ABSL_ARRAYSIZE(buf), 0,
                                        reinterpret_cast<sockaddr*>(&storage), sizeof(storage)));

  // Make sure the drop count is now 2.
  Utility::readFromSocket(handle, *address, processor, time, false, &packets_dropped);
  EXPECT_EQ(2, packets_dropped);
}
#endif

} // namespace
} // namespace Network
} // namespace Envoy
