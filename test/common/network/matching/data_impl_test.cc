#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Network {
namespace Matching {
namespace {

// Returns corresponding IPv4 source value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv4SourceOnly) {
  auto source = Address::Ipv4Instance("1.2.3.4", 5678);

  auto data = NetworkMatchingDataImpl(source.ip(), nullptr);

  EXPECT_EQ("1.2.3.4", data.sourceIp()->addressAsString());
  EXPECT_EQ(5678, data.sourcePort().value());
}

// Returns corresponding IPv4 destination value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv4DestinationOnly) {
  auto destination = Address::Ipv4Instance("1.2.3.4", 5678);

  auto data = NetworkMatchingDataImpl(nullptr, destination.ip());

  EXPECT_EQ("1.2.3.4", data.destinationIp()->addressAsString());
  EXPECT_EQ(5678, data.destinationPort().value());
}

// Returns corresponding IPv6 source value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv6SourceOnly) {
  auto source = Address::Ipv6Instance("1234:5678:90ab:cdef:1234:5678:90ab:cdef", 1234);

  auto data = NetworkMatchingDataImpl(source.ip(), nullptr);

  EXPECT_EQ("1234:5678:90ab:cdef:1234:5678:90ab:cdef", data.sourceIp()->addressAsString());
  EXPECT_EQ(1234, data.sourcePort().value());
}

// Returns corresponding IPv6 destination value in network matching data.
TEST(NetworkMatchingDataImpl, Ipv6DestinationOnly) {
  auto destination = Address::Ipv6Instance("1234:5678:90ab:cdef:1234:5678:90ab:cdef", 1234);

  auto data = NetworkMatchingDataImpl(nullptr, destination.ip());

  EXPECT_EQ("1234:5678:90ab:cdef:1234:5678:90ab:cdef", data.destinationIp()->addressAsString());
  EXPECT_EQ(1234, data.destinationPort().value());
}

} // namespace
} // namespace Matching
} // namespace Network
} // namespace Envoy
