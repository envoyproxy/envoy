#include <arpa/inet.h>

#include "envoy/api/v2/core/address.pb.h"
#include "envoy/network/resolver.h"

#include "common/network/address_impl.h"
#include "common/network/address_range_impl.h"
#include "common/network/resolver_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Between;
using testing::Return;

namespace Envoy {
namespace Network {
namespace Address {
namespace {

void makeFdBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  ASSERT_GE(flags, 0);
  ASSERT_EQ(::fcntl(fd, F_SETFL, flags & (~O_NONBLOCK)), 0);
}

// Make an IP instance suitable for testing.
InstanceConstSharedPtr makeIpVersionedInstance(IpVersion version) {
  switch (version) {
  case IpVersion::v4:
    return std::make_shared<Ipv4Instance>("127.0.0.1", 0u);
  case IpVersion::v6:
    return std::make_shared<Ipv6Instance>("::0001", 0u);
  }
  return nullptr;
}

// Return a IpInstance when version doesn't matter
InstanceConstSharedPtr makeIpInstance() { return makeIpVersionedInstance(IpVersion::v4); }

// Return the port bound to the fd.
uint32_t getPort(int fd) {
  struct sockaddr address;
  socklen_t address_len(sizeof(address));
  if (getsockname(fd, &address, &address_len) < 0) {
    return -1;
  }
  switch (address.sa_family) {
  case AF_INET:
    struct sockaddr_in address_in;
    address_len = sizeof(address_in);
    if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&address_in), &address_len) < 0) {
      return -1;
    }
    return ntohs(address_in.sin_port);
  case AF_INET6:
    struct sockaddr_in6 address_in6;
    address_len = sizeof(address_in6);
    if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&address_in6), &address_len) < 0) {
      return -1;
    }
    return ntohs(address_in6.sin6_port);
  default:
    return -1;
  }
}

} // namespace

// Confirm a simple instantiation works correctly.
TEST(IpRangeInstanceTest, Simple) {
  Runtime::RandomGeneratorImpl random;
  InstanceConstSharedPtr instance(std::make_shared<Ipv4Instance>("127.0.0.1", 0u));
  IpInstanceRange test_range(std::move(instance), "2048-2049", random);
  EXPECT_EQ("127.0.0.1:2048-2049", test_range.asString());
  EXPECT_EQ("127.0.0.1:2048-2049", test_range.logicalName());
  EXPECT_EQ(Type::Ip, test_range.type());
  EXPECT_EQ("127.0.0.1", test_range.ip()->addressAsString());
  EXPECT_EQ(0u, test_range.ip()->port());
  EXPECT_EQ(IpVersion::v4, test_range.ip()->version());
  EXPECT_EQ(2048u, test_range.startPort());
  EXPECT_EQ(2049u, test_range.endPort());
}

// Confirm providing an instance with a specified port errors.
TEST(IpRangeInstanceTest, BadPortInInstance) {
  InstanceConstSharedPtr instance(std::make_shared<Ipv4Instance>("127.0.0.1", 3u));
  Runtime::RandomGeneratorImpl random;
  EXPECT_THROW(IpInstanceRange test_range(std::move(instance), "2048-2049", random),
               EnvoyException);
}

// Try several variations on format and confirm they do or don't error as appropriate.
TEST(IpRangeInstanceTest, FormatVariations) {
  Runtime::RandomGeneratorImpl random;

  std::unique_ptr<IpInstanceRange> ptr;

  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2048", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2048-2049-2050", random)),
               EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "-2050", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2049-2048", random)),
               EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2048-", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "-", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2048:2049", random)),
               EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "a-2049", random)), EnvoyException);
  EXPECT_THROW(ptr.reset(new IpInstanceRange(makeIpInstance(), "2049-b", random)), EnvoyException);

  ptr.reset(new IpInstanceRange(makeIpInstance(), "2048-2049 ", random));
  EXPECT_EQ(2048u, ptr->startPort());
  EXPECT_EQ(2049u, ptr->endPort());

  ptr.reset(new IpInstanceRange(makeIpInstance(), " 2048-2049", random));
  EXPECT_EQ(2048u, ptr->startPort());
  EXPECT_EQ(2049u, ptr->endPort());

  ptr.reset(new IpInstanceRange(makeIpInstance(), "2048 - 2049", random));
  EXPECT_EQ(2048u, ptr->startPort());
  EXPECT_EQ(2049u, ptr->endPort());

  ptr.reset(new IpInstanceRange(makeIpInstance(), "2048-2048", random));
  EXPECT_EQ(2048u, ptr->startPort());
  EXPECT_EQ(2048u, ptr->endPort());
}

// Test operator==() behavior.
TEST(IpRangeInstanceTest, Equals) {
  Runtime::RandomGeneratorImpl random;
  IpInstanceRange range1(makeIpInstance(), "2048-2049", random);
  IpInstanceRange range2(makeIpInstance(), "2048-2049", random);
  EXPECT_EQ(range1, range2);
  IpInstanceRange range3(makeIpInstance(), "2048-2050", random);
  EXPECT_NE(range1, range3);
  IpInstanceRange range4(std::make_shared<Ipv4Instance>("127.0.0.2", 0u), "2048-2049", random);
  EXPECT_NE(range1, range4);
}

// Confirm connect() throws.
TEST(IpRangeInstanceTest, Connect) {
  // Setup a port to connect to.
  auto addr_port = Network::Utility::parseInternetAddressAndPort(
      fmt::format("{}:0", Network::Test::getAnyAddressUrlString(IpVersion::v4)), false);
  ASSERT_NE(addr_port, nullptr);
  if (addr_port->ip()->port() == 0) {
    addr_port = Network::Test::findOrCheckFreePort(addr_port, SocketType::Stream);
  }
  ASSERT_NE(addr_port, nullptr);
  ASSERT_NE(addr_port->ip(), nullptr);
  const int listen_fd = addr_port->socket(SocketType::Stream);
  ASSERT_GE(listen_fd, 0) << addr_port->asString();
  ScopedFdCloser closer1(listen_fd);

  const int rc = addr_port->bind(listen_fd);
  const int err = errno;
  ASSERT_EQ(rc, 0) << addr_port->asString() << "\nerror: " << strerror(err) << "\nerrno: " << err;
  ASSERT_EQ(::listen(listen_fd, 128), 0);

  // Create an instance range and try to connect through it.
  Runtime::RandomGeneratorImpl random;
  IpInstanceRange instance_range(
      std::make_shared<Ipv4Instance>(addr_port->ip()->addressAsString()),
      absl::StrCat(addr_port->ip()->port(), "-", addr_port->ip()->port()), random);
  int client_fd = instance_range.socket(SocketType::Stream);
  ScopedFdCloser closer2(client_fd);
  makeFdBlocking(client_fd);
  EXPECT_THROW(instance_range.connect(client_fd), EnvoyException);
}

// bind() does different things on different IP versions, so separate tests for the
// two versions.
class IpVersionRangeInstanceTest : public testing::TestWithParam<IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, IpVersionRangeInstanceTest,
                        ::testing::Values(IpVersion::v4, IpVersion::v6));

// Confirm a non EADDRINUSE error is returned immediately
TEST_P(IpVersionRangeInstanceTest, BindNonUseErrorReturns) {
  // Should only get one call, doesn't matter what to return.
  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, random()).WillOnce(Return(100u));

  IpInstanceRange instance_range(makeIpVersionedInstance(GetParam()), "400-400", random);
  int fd = instance_range.socket(SocketType::Stream);
  EXPECT_GE(fd, 0);
  ScopedFdCloser closer1(fd);

  // Should fail immediately as < 1024 is a reserved port.
  EXPECT_EQ(EACCES, instance_range.bind(fd));
  testing::Mock::VerifyAndClearExpectations(&random);
}

// Confirm that if a port is taken, the implementation retries.
TEST_P(IpVersionRangeInstanceTest, RetryOnCollision) {
  InstanceConstSharedPtr blocking_instance(makeIpVersionedInstance(GetParam()));
  int blocking_fd = blocking_instance->socket(SocketType::Stream);
  ScopedFdCloser closer1(blocking_fd);
  EXPECT_EQ(0, blocking_instance->bind(blocking_fd));
  uint32_t port = getPort(blocking_fd);
  ASSERT_NE(0u, port);

  // Return one value in the low half of the range, one in the high half.
  // The first will cause a collision, the second will not.
  // Note that there is the possibility of colliding with other stuff happening on the machine, so
  // the only thing this test is checking is whether or not the RNG was called more than once.
  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, random())
      .Times(Between(2, 4))
      .WillOnce(Return(100u))
      .WillRepeatedly(Return(std::numeric_limits<uint64_t>::max()));

  IpInstanceRange range(makeIpVersionedInstance(GetParam()), absl::StrCat(port, "-", port + 1),
                        random);

  int probe_fd = range.socket(SocketType::Stream);
  ScopedFdCloser closer2(probe_fd);
  range.bind(probe_fd);

  testing::Mock::VerifyAndClearExpectations(&random);
}

// Confirm that after several retries, the implementation gives up.
TEST_P(IpVersionRangeInstanceTest, EnoughRetriesFail) {
  const int kExpectedRetries = 4;

  InstanceConstSharedPtr blocking_instance(makeIpVersionedInstance(GetParam()));
  int blocking_fd = blocking_instance->socket(SocketType::Stream);
  ScopedFdCloser closer1(blocking_fd);
  EXPECT_EQ(0, blocking_instance->bind(blocking_fd));
  uint32_t port = getPort(blocking_fd);
  ASSERT_NE(0u, port);

  // Return one value in the low half of the range, one in the high half.
  // The first will cause a collision, the second will not.
  // Note that there is the possibility of colliding with other stuff happening on the machine, so
  // the only thing this test is checking is whether or not the RNG was called more than once.
  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, random()).Times(kExpectedRetries).WillRepeatedly(Return(100u));

  IpInstanceRange range(makeIpVersionedInstance(GetParam()), absl::StrCat(port, "-", port + 1),
                        random);

  int probe_fd = range.socket(SocketType::Stream);
  ScopedFdCloser closer2(probe_fd);
  EXPECT_EQ(EADDRINUSE, range.bind(probe_fd));

  testing::Mock::VerifyAndClearExpectations(&random);
}

// Confirm that a port above the top of the range is never allocated.
TEST_P(IpVersionRangeInstanceTest, TopRangeBound) {
  // Find an available port.
  InstanceConstSharedPtr blocking_instance(makeIpVersionedInstance(GetParam()));
  int blocking_fd = blocking_instance->socket(SocketType::Stream);
  EXPECT_EQ(0, blocking_instance->bind(blocking_fd));
  uint32_t port = getPort(blocking_fd);
  ASSERT_NE(0u, port);

  close(blocking_fd);

  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, random()).WillOnce(Return(std::numeric_limits<uint64_t>::max()));

  IpInstanceRange range(makeIpVersionedInstance(GetParam()), absl::StrCat(port - 1, "-", port),
                        random);
  int probe_fd = range.socket(SocketType::Stream);
  ScopedFdCloser closer2(probe_fd);
  EXPECT_EQ(0, range.bind(probe_fd));
  EXPECT_EQ(port, getPort(probe_fd));
}

// Confirm that a port above the top of the range is never allocated.
TEST_P(IpVersionRangeInstanceTest, BottomRangeBound) {
  // Find an available port.
  InstanceConstSharedPtr blocking_instance(makeIpVersionedInstance(GetParam()));
  int blocking_fd = blocking_instance->socket(SocketType::Stream);
  EXPECT_EQ(0, blocking_instance->bind(blocking_fd));
  uint32_t port = getPort(blocking_fd);
  close(blocking_fd);
  ASSERT_LT(1u, port); // So port-1 is still valid.

  Runtime::MockRandomGenerator random;
  EXPECT_CALL(random, random()).WillOnce(Return(0u));

  IpInstanceRange range(makeIpVersionedInstance(GetParam()), absl::StrCat(port, "-", port + 1),
                        random);
  int probe_fd = range.socket(SocketType::Stream);
  ScopedFdCloser closer2(probe_fd);
  EXPECT_EQ(0, range.bind(probe_fd));
  EXPECT_EQ(port, getPort(probe_fd));
}

// Confirm an IpRangeInstance is created correctly from resolving the right kind of
// API instance.
TEST(IpRangeResolverTest, Simple) {
  envoy::api::v2::core::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_named_port("3000-3001");
  socket_address.set_resolver_name("google.ipRange");
  InstanceConstSharedPtr result(resolveProtoSocketAddress(socket_address));

  EXPECT_TRUE(result);
  EXPECT_TRUE(result->ip());
  EXPECT_EQ(result->asString(), "1.2.3.4:3000-3001");
}

// Confirm this resolver requires the setting of a named port.
TEST(IpRangeResolverTest, RequireNamedPort) {
  envoy::api::v2::core::SocketAddress socket_address;
  socket_address.set_address("1.2.3.4");
  socket_address.set_port_value(2000);
  socket_address.set_resolver_name("google.ipRange");
  EXPECT_THROW(InstanceConstSharedPtr result(resolveProtoSocketAddress(socket_address)),
               EnvoyException);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
