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
  UdpStatsdSink sink(tls_, Network::Test::getCanonicalLoopbackAddress(GetParam()));
  int fd = sink.getFdForTests();
  sink.flushCounter("test_counter", 1);
  EXPECT_NE(fd, -1);

  // Check that fd has not changed.
  sink.flushGauge("test_gauge", 1);
  sink.onTimespanComplete("test_counter", std::chrono::milliseconds(5));
  EXPECT_EQ(fd, sink.getFdForTests());

  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ("127.0.0.1", Network::Address::addressFromFd(fd)->ip()->addressAsString());
  } else {
    EXPECT_EQ("::1", Network::Address::addressFromFd(fd)->ip()->addressAsString());
  }
  tls_.shutdownThread();
}

} // Statsd
} // Stats
} // Envoy
