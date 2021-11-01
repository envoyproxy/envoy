#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Network {
namespace Matching {
namespace {

// Returns corresponding source IPv4 value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv4SourceIp) {
  auto source = Address::Ipv4Instance("1.2.3.4", 5678);

  auto data = NetworkMatchingDataImpl(source.ip());

  EXPECT_EQ("1.2.3.4", data.sourceIp()->addressAsString());
}

// Returns corresponding source IPv6 value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv6SourceIp) {
  auto source = Address::Ipv6Instance("1234:5678:90ab:cdef:1234:5678:90ab:cdef", 1234);

  auto data = NetworkMatchingDataImpl(source.ip());

  EXPECT_EQ("1234:5678:90ab:cdef:1234:5678:90ab:cdef", data.sourceIp()->addressAsString());
}

} // namespace
} // namespace Matching
} // namespace Network
} // namespace Envoy
