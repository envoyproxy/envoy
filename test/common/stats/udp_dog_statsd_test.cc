#include <chrono>

#include "envoy/stats/stats.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"
#include "common/stats/dog_statsd.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Stats {
namespace DogStatsd {

class UdpStatsdSinkTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_CASE_P(IpVersions, UdpStatsdSinkTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(UdpStatsdSinkTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  // UDP statsd server address.
  Network::Address::InstanceConstSharedPtr server_address =
      Network::Utility::parseInternetAddressAndPort(
          fmt::format("{}:8125", Network::Test::getLoopbackAddressUrlString(GetParam())));
  UdpStatsdSink sink(tls_, server_address);
  int fd = sink.getFdForTests();
  EXPECT_NE(fd, -1);

  // Check that fd has not changed.
  std::vector<Tag> tags = {Tag{"node", "test"}};
  std::string name = "test_counter";
  NiceMock<MockCounter> counter;
  counter.name_ = name;
  ON_CALL(counter, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(counter, tags()).WillByDefault(ReturnRef(tags));

  sink.flushCounter(counter, 1);

  name = "test_gauge";
  NiceMock<MockGauge> gauge;
  gauge.name_ = name;
  ON_CALL(gauge, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(gauge, tags()).WillByDefault(ReturnRef(tags));
  sink.flushGauge(gauge, 1);

  name = "test_timer";
  NiceMock<MockHistogram> timer;
  timer.name_ = name;
  ON_CALL(timer, tagExtractedName()).WillByDefault(ReturnRef(name));
  ON_CALL(timer, tags()).WillByDefault(ReturnRef(tags));

  EXPECT_EQ(fd, sink.getFdForTests());

  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_EQ("127.0.0.1:8125", Network::Address::peerAddressFromFd(fd)->asString());
  } else {
    EXPECT_EQ("[::1]:8125", Network::Address::peerAddressFromFd(fd)->asString());
  }
  tls_.shutdownThread();
}

} // namespace Statsd
} // namespace Stats
} // namespace Envoy
