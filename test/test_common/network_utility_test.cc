#include <string>

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace Test {

class NetworkUtilityTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  NetworkUtilityTest() : version_(GetParam()) {}
  const Address::IpVersion version_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, NetworkUtilityTest,
                        testing::ValuesIn(TestEnvironment::getIpTestParameters()));

// This validates Network::Test::bindFreeLoopbackPort behaves as desired, i.e. that we don't have
// a significant risk of flakes due to re-use of a port over short time intervals. We can't drive
// this too long else we'll eventually run out of ephemeral ports.
//
// Tested: IPv4 with --gtest_repeats=1000 and kLimit=1000 on Ubuntu with docker.
// Result: Zero failures, presumably because of the randomization of the address.
//
// Tested: IPv6 --gtest_repeats=1000 and kLimit=50 on Ubuntu with docker.
// Result: In about 5% of runs, two of the 50 allocated ports were the the same, though not
//         more than that.
// The test is DISABLED as we don't want the occasional expected collisions to cause problems.
TEST_P(NetworkUtilityTest, DISABLED_ValidateBindFreeLoopbackPort) {
  std::map<std::string, size_t> seen;
  const size_t kLimit = 50;
  for (size_t n = 0; n < kLimit; ++n) {
    auto addr_fd = Network::Test::bindFreeLoopbackPort(version_, Address::SocketType::Stream);
    close(addr_fd.second);
    auto addr = addr_fd.first->asString();
    auto search = seen.find(addr);
    if (search != seen.end()) {
      ADD_FAILURE() << "Saw duplicate binds for address " << addr << " at steps " << n << " and "
                    << search->second;
    }
    seen[addr] = n;
  }
}

} // namespace Test
} // namespace Network
} // namespace Envoy
