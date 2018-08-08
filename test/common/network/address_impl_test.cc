#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "envoy/common/exception.h"

#include "common/common/fmt.h"
#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Address {
namespace {
bool addressesEqual(const InstanceConstSharedPtr& a, const Instance& b) {
  if (a == nullptr || a->type() != Type::Ip || b.type() != Type::Ip) {
    return false;
  } else {
    return a->ip()->addressAsString() == b.ip()->addressAsString();
  }
}

void makeFdBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  ASSERT_GE(flags, 0);
  ASSERT_EQ(::fcntl(fd, F_SETFL, flags & (~O_NONBLOCK)), 0);
}

void testSocketBindAndConnect(Network::Address::IpVersion ip_version, bool v6only) {
  auto addr_port = Network::Utility::parseInternetAddressAndPort(
      fmt::format("{}:0", Network::Test::getAnyAddressUrlString(ip_version)), v6only);
  ASSERT_NE(addr_port, nullptr);

  if (addr_port->ip()->port() == 0) {
    addr_port = Network::Test::findOrCheckFreePort(addr_port, SocketType::Stream);
  }
  ASSERT_NE(addr_port, nullptr);
  ASSERT_NE(addr_port->ip(), nullptr);

  // Create a socket on which we'll listen for connections from clients.
  const int listen_fd = addr_port->socket(SocketType::Stream);
  ASSERT_GE(listen_fd, 0) << addr_port->asString();
  ScopedFdCloser closer1(listen_fd);

  // Check that IPv6 sockets accept IPv6 connections only.
  if (addr_port->ip()->version() == IpVersion::v6) {
    int socket_v6only = 0;
    socklen_t size_int = sizeof(socket_v6only);
    ASSERT_GE(::getsockopt(listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &socket_v6only, &size_int), 0);
    EXPECT_EQ(v6only, socket_v6only);
  }

  // Bind the socket to the desired address and port.
  const Api::SysCallResult result = addr_port->bind(listen_fd);
  ASSERT_EQ(result.rc_, 0) << addr_port->asString() << "\nerror: " << strerror(result.errno_)
                           << "\nerrno: " << result.errno_;

  // Do a bare listen syscall. Not bothering to accept connections as that would
  // require another thread.
  ASSERT_EQ(::listen(listen_fd, 128), 0);

  auto client_connect = [](Address::InstanceConstSharedPtr addr_port) {
    // Create a client socket and connect to the server.
    const int client_fd = addr_port->socket(SocketType::Stream);
    ASSERT_GE(client_fd, 0) << addr_port->asString();
    ScopedFdCloser closer2(client_fd);

    // Instance::socket creates a non-blocking socket, which that extends all the way to the
    // operation of ::connect(), so connect returns with errno==EWOULDBLOCK before the tcp
    // handshake can complete. For testing convenience, re-enable blocking on the socket
    // so that connect will wait for the handshake to complete.
    makeFdBlocking(client_fd);

    // Connect to the server.
    const Api::SysCallResult result = addr_port->connect(client_fd);
    ASSERT_EQ(result.rc_, 0) << addr_port->asString() << "\nerror: " << strerror(result.errno_)
                             << "\nerrno: " << result.errno_;
  };

  client_connect(addr_port);

  if (!v6only) {
    ASSERT_EQ(IpVersion::v6, addr_port->ip()->version());
    auto v4_addr_port = Network::Utility::parseInternetAddress(
        Network::Test::getLoopbackAddressUrlString(Network::Address::IpVersion::v4),
        addr_port->ip()->port(), true);
    ASSERT_NE(v4_addr_port, nullptr);
    client_connect(v4_addr_port);
  }
}
} // namespace

class AddressImplSocketTest : public testing::TestWithParam<IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, AddressImplSocketTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(AddressImplSocketTest, SocketBindAndConnect) {
  // Test listening on and connecting to an unused port with an IP loopback address.
  testSocketBindAndConnect(GetParam(), true);
}

TEST(Ipv4CompatAddressImplSocktTest, SocketBindAndConnect) {
  if (TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v6)) {
    testSocketBindAndConnect(Network::Address::IpVersion::v6, false);
  }
}

TEST(Ipv4InstanceTest, SocketAddress) {
  sockaddr_in addr4;
  addr4.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &addr4.sin_addr));
  addr4.sin_port = htons(6502);

  Ipv4Instance address(&addr4);
  EXPECT_EQ("1.2.3.4:6502", address.asString());
  EXPECT_EQ("1.2.3.4:6502", address.logicalName());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("1.2.3.4", address.ip()->addressAsString());
  EXPECT_EQ(6502U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("1.2.3.4"), address));
  EXPECT_EQ(nullptr, address.ip()->ipv6());
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, AddressOnly) {
  Ipv4Instance address("3.4.5.6");
  EXPECT_EQ("3.4.5.6:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("3.4.5.6", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("3.4.5.6"), address));
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, AddressAndPort) {
  Ipv4Instance address("127.0.0.1", 80);
  EXPECT_EQ("127.0.0.1:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("127.0.0.1", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("127.0.0.1"), address));
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, PortOnly) {
  Ipv4Instance address(443);
  EXPECT_EQ("0.0.0.0:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("0.0.0.0", address.ip()->addressAsString());
  EXPECT_TRUE(address.ip()->isAnyAddress());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("0.0.0.0"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, Multicast) {
  Ipv4Instance address("230.0.0.1");
  EXPECT_EQ("230.0.0.1:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("230.0.0.1", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("230.0.0.1"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, Broadcast) {
  Ipv4Instance address("255.255.255.255");
  EXPECT_EQ("255.255.255.255:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("255.255.255.255", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("255.255.255.255"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv4InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv4Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv4Instance("bar", 1), EnvoyException);
}

TEST(Ipv6InstanceTest, SocketAddress) {
  sockaddr_in6 addr6;
  addr6.sin6_family = AF_INET6;
  EXPECT_EQ(1, inet_pton(AF_INET6, "01:023::00Ef", &addr6.sin6_addr));
  addr6.sin6_port = htons(32000);

  Ipv6Instance address(addr6);
  EXPECT_EQ("[1:23::ef]:32000", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("1:23::ef", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(32000U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("1:0023::0Ef"), address));
  EXPECT_EQ(nullptr, address.ip()->ipv4());
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, AddressOnly) {
  Ipv6Instance address("2001:0db8:85a3:0000:0000:8a2e:0370:7334");
  EXPECT_EQ("[2001:db8:85a3::8a2e:370:7334]:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("2001:db8:85a3::8a2e:370:7334", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(
      Network::Utility::parseInternetAddress("2001:db8:85a3::8a2e:0370:7334"), address));
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, AddressAndPort) {
  Ipv6Instance address("::0001", 80);
  EXPECT_EQ("[::1]:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::1", address.ip()->addressAsString());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("0:0:0:0:0:0:0:1"), address));
  EXPECT_TRUE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, PortOnly) {
  Ipv6Instance address(443);
  EXPECT_EQ("[::]:443", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("::", address.ip()->addressAsString());
  EXPECT_TRUE(address.ip()->isAnyAddress());
  EXPECT_EQ(443U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(Network::Utility::parseInternetAddress("::0000"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, Multicast) {
  Ipv6Instance address("FF00::");
  EXPECT_EQ("[ff00::]:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("ff00::", address.ip()->addressAsString());
  EXPECT_FALSE(address.ip()->isAnyAddress());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(
      Network::Utility::parseInternetAddress("FF00:0000:0000:0000:0000:0000:0000:0000"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, Broadcast) {
  Ipv6Instance address("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF");
  EXPECT_EQ("[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:0", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", address.ip()->addressAsString());
  EXPECT_EQ(0U, address.ip()->port());
  EXPECT_EQ(IpVersion::v6, address.ip()->version());
  EXPECT_TRUE(addressesEqual(
      Network::Utility::parseInternetAddress("FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"), address));
  EXPECT_FALSE(address.ip()->isUnicastAddress());
}

TEST(Ipv6InstanceTest, BadAddress) {
  EXPECT_THROW(Ipv6Instance("foo"), EnvoyException);
  EXPECT_THROW(Ipv6Instance("bar", 1), EnvoyException);
}

TEST(PipeInstanceTest, Basic) {
  PipeInstance address("/foo");
  EXPECT_EQ("/foo", address.asString());
  EXPECT_EQ(Type::Pipe, address.type());
  EXPECT_EQ(nullptr, address.ip());
}

TEST(PipeInstanceTest, AbstractNamespace) {
#if defined(__linux__)
  PipeInstance address("@/foo");
  EXPECT_EQ("@/foo", address.asString());
  EXPECT_EQ(Type::Pipe, address.type());
  EXPECT_EQ(nullptr, address.ip());
#else
  EXPECT_THROW(PipeInstance address("@/foo"), EnvoyException);
#endif
}

TEST(PipeInstanceTest, BadAddress) {
  std::string long_address(1000, 'X');
  EXPECT_THROW_WITH_REGEX(PipeInstance address(long_address), EnvoyException,
                          "exceeds maximum UNIX domain socket path size");
}

TEST(PipeInstanceTest, UnlinksExistingFile) {
  const auto bind_uds_socket = [](const std::string& path) {
    PipeInstance address(path);
    const int listen_fd = address.socket(SocketType::Stream);
    ASSERT_GE(listen_fd, 0) << address.asString();
    ScopedFdCloser closer(listen_fd);

    const Api::SysCallResult result = address.bind(listen_fd);
    ASSERT_EQ(result.rc_, 0) << address.asString() << "\nerror: " << strerror(result.errno_)
                             << "\nerrno: " << result.errno_;
  };

  const std::string path = TestEnvironment::unixDomainSocketPath("UnlinksExistingFile.sock");
  bind_uds_socket(path);
  bind_uds_socket(path); // after closing, second bind to the same path should succeed.
}

TEST(AddressFromSockAddr, IPv4) {
  sockaddr_storage ss;
  auto& sin = reinterpret_cast<sockaddr_in&>(ss);

  sin.sin_family = AF_INET;
  EXPECT_EQ(1, inet_pton(AF_INET, "1.2.3.4", &sin.sin_addr));
  sin.sin_port = htons(6502);

  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, 1), "ss_len");
  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, sizeof(sockaddr_in) - 1), "ss_len");
  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, sizeof(sockaddr_in) + 1), "ss_len");

  EXPECT_EQ("1.2.3.4:6502", addressFromSockAddr(ss, sizeof(sockaddr_in))->asString());

  // Invalid family.
  sin.sin_family = AF_UNSPEC;
  EXPECT_THROW(addressFromSockAddr(ss, sizeof(sockaddr_in)), EnvoyException);
}

TEST(AddressFromSockAddr, IPv6) {
  sockaddr_storage ss;
  auto& sin6 = reinterpret_cast<sockaddr_in6&>(ss);

  sin6.sin6_family = AF_INET6;
  EXPECT_EQ(1, inet_pton(AF_INET6, "01:023::00Ef", &sin6.sin6_addr));
  sin6.sin6_port = htons(32000);

  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, 1), "ss_len");
  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, sizeof(sockaddr_in6) - 1), "ss_len");
  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, sizeof(sockaddr_in6) + 1), "ss_len");

  EXPECT_EQ("[1:23::ef]:32000", addressFromSockAddr(ss, sizeof(sockaddr_in6))->asString());

  // Test that IPv4-mapped IPv6 address is returned as an Ipv4Instance when 'v6only' parameter is
  // 'false', but not otherwise.
  EXPECT_EQ(1, inet_pton(AF_INET6, "::ffff:192.0.2.128", &sin6.sin6_addr));
  EXPECT_EQ(IpVersion::v4, addressFromSockAddr(ss, sizeof(sockaddr_in6), false)->ip()->version());
  EXPECT_EQ("192.0.2.128:32000", addressFromSockAddr(ss, sizeof(sockaddr_in6), false)->asString());
  EXPECT_EQ(IpVersion::v6, addressFromSockAddr(ss, sizeof(sockaddr_in6), true)->ip()->version());
  EXPECT_EQ("[::ffff:192.0.2.128]:32000",
            addressFromSockAddr(ss, sizeof(sockaddr_in6), true)->asString());
}

TEST(AddressFromSockAddr, Pipe) {
  sockaddr_storage ss;
  auto& sun = reinterpret_cast<sockaddr_un&>(ss);
  sun.sun_family = AF_UNIX;

  StringUtil::strlcpy(sun.sun_path, "/some/path", sizeof sun.sun_path);

  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, 1), "ss_len");
  EXPECT_DEATH_LOG_TO_STDERR(addressFromSockAddr(ss, offsetof(struct sockaddr_un, sun_path)),
                             "ss_len");

  socklen_t ss_len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen(sun.sun_path);
  EXPECT_EQ("/some/path", addressFromSockAddr(ss, ss_len)->asString());

  // Abstract socket namespace.
  StringUtil::strlcpy(&sun.sun_path[1], "/some/abstract/path", sizeof sun.sun_path);
  sun.sun_path[0] = '\0';
  ss_len = offsetof(struct sockaddr_un, sun_path) + 1 + strlen("/some/abstract/path");
#if defined(__linux__)
  EXPECT_EQ("@/some/abstract/path", addressFromSockAddr(ss, ss_len)->asString());
#else
  EXPECT_THROW(addressFromSockAddr(ss, ss_len), EnvoyException);
#endif
}

// Test comparisons between all the different (known) test classes.
struct TestCase {
  enum InstanceType { Ipv4, Ipv6, Pipe };

  TestCase() : type_(Ipv4), port_(0) {}
  TestCase(enum InstanceType type, std::string address, uint32_t port)
      : type_(type), address_(address), port_(port) {}
  TestCase(const TestCase& rhs) : type_(rhs.type_), address_(rhs.address_), port_(rhs.port_) {}

  bool operator==(const TestCase& rhs) {
    return (type_ == rhs.type_ && address_ == rhs.address_ && port_ == rhs.port_);
  }

  enum InstanceType type_;
  std::string address_;
  uint32_t port_; // Ignored for Pipe
};

class MixedAddressTest : public testing::TestWithParam<::testing::tuple<TestCase, TestCase>> {
public:
protected:
  InstanceConstSharedPtr testCaseToInstance(const struct TestCase& test_case) {
    // Catch default construction.
    if (test_case.address_ == "") {
      return nullptr;
    }
    switch (test_case.type_) {
    case TestCase::Ipv4:
      return std::make_shared<Ipv4Instance>(test_case.address_, test_case.port_);
      break;
    case TestCase::Ipv6:
      return std::make_shared<Ipv6Instance>(test_case.address_, test_case.port_);
      break;
    case TestCase::Pipe:
      return std::make_shared<PipeInstance>(test_case.address_);
      break;
    }
    return nullptr;
  }
};

TEST_P(MixedAddressTest, Equality) {
  TestCase lhs_case = ::testing::get<0>(GetParam());
  TestCase rhs_case = ::testing::get<1>(GetParam());
  InstanceConstSharedPtr lhs = testCaseToInstance(lhs_case);
  InstanceConstSharedPtr rhs = testCaseToInstance(rhs_case);
  if (lhs_case == rhs_case) {
    EXPECT_EQ(*lhs, *rhs) << lhs->asString() << " != " << rhs->asString();
  } else {
    EXPECT_NE(*lhs, *rhs) << lhs->asString() << " == " << rhs->asString();
  }
}

struct TestCase test_cases[] = {
    {TestCase::Ipv4, "1.2.3.4", 1},         {TestCase::Ipv4, "1.2.3.4", 2},
    {TestCase::Ipv4, "1.2.3.5", 1},         {TestCase::Ipv6, "01:023::00ef", 1},
    {TestCase::Ipv6, "01:023::00ef", 2},    {TestCase::Ipv6, "01:023::00ed", 1},
    {TestCase::Pipe, "/path/to/pipe/1", 0}, {TestCase::Pipe, "/path/to/pipe/2", 0}};

INSTANTIATE_TEST_CASE_P(AddressCrossProduct, MixedAddressTest,
                        ::testing::Combine(::testing::ValuesIn(test_cases),
                                           ::testing::ValuesIn(test_cases)));

} // namespace Address
} // namespace Network
} // namespace Envoy
