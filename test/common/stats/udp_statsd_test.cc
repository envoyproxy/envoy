#include <sys/socket.h>

#include <chrono>

#include "common/network/address_impl.h"
#include "common/stats/statsd.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
using testing::NiceMock;

namespace Stats {
namespace Statsd {

class UdpStatsdSinkTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, UdpStatsdSinkTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(UdpStatsdSinkTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_,
                     fmt::format("{}:0", Network::Test::getLoopbackAddressUrlString(GetParam())));
  // Creates and connects to socket.
  sink.flushCounter("test_counter", 1);
  int fd = sink.getFdForTests();
  EXPECT_NE(fd, -1);

  // Check that fd has not changed.
  sink.flushGauge("test_gauge", 1);
  sink.onTimespanComplete("test_counter", std::chrono::milliseconds(5));
  EXPECT_EQ(fd, sink.getFdForTests());

  struct sockaddr_storage sockaddress;
  socklen_t sock_len = sizeof(sockaddress);
  EXPECT_EQ(0, getsockname(fd, reinterpret_cast<struct sockaddr*>(&sockaddress), &sock_len));

  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ("127.0.0.1", Network::Address::addressFromSockAddr(sockaddress, sizeof(sockaddr_in))
                               ->ip()
                               ->addressAsString());
  } else {
    EXPECT_EQ("::1", Network::Address::addressFromSockAddr(sockaddress, sizeof(sockaddr_in6))
                         ->ip()
                         ->addressAsString());
  }
  tls_.shutdownThread();
}

// Regression Test
TEST(UdpStatsdSinkTest, InitWithPort) {
  if (TestEnvironment::shouldRunTestForIpVersion(Network::Address::IpVersion::v4)) {
    NiceMock<ThreadLocal::MockInstance> tls_;
    UdpStatsdSink sink(tls_, 0);
    // Creates and connects to socket.
    sink.flushCounter("test_counter", 1);
    int fd = sink.getFdForTests();
    EXPECT_NE(fd, -1);
    struct sockaddr_storage sockaddress;
    socklen_t sock_len = sizeof(sockaddress);

    EXPECT_EQ(0, getsockname(fd, reinterpret_cast<struct sockaddr*>(&sockaddress), &sock_len));
    EXPECT_EQ("127.0.0.1", Network::Address::addressFromSockAddr(sockaddress, sizeof(sockaddr_in))
                               ->ip()
                               ->addressAsString());
    tls_.shutdownThread();
  }
}

} // Statsd
} // Stats
} // Envoy
