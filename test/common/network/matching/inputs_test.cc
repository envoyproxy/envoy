#include "source/common/network/address_impl.h"
#include "source/common/network/matching/data_impl.h"
#include "source/common/network/matching/inputs.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Network {
namespace Matching {
namespace {

// Returns corresponding IPv4 input matching data in network matching data.
TEST(IpDataInputBase, Ipv4DataAvailable) {
  auto address = Address::Ipv4Instance("1.2.3.4", 5678);

  auto data = NetworkMatchingDataImpl(address.ip(), address.ip());

  auto source_data_input = SourceIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            source_data_input.get(data).data_availability_);
  EXPECT_EQ("1.2.3.4", source_data_input.get(data).data_);

  auto destination_data_input = DestinationIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            destination_data_input.get(data).data_availability_);
  EXPECT_EQ("1.2.3.4", destination_data_input.get(data).data_);
}

// Returns corresponding IPv6 input matching data in network matching data.
TEST(IpDataInputBase, Ipv6DataAvailable) {
  auto address = Address::Ipv6Instance("1234:5678:90ab:cdef:1234:5678:90ab:cdef", 1234);

  auto data = NetworkMatchingDataImpl(address.ip(), address.ip());

  auto source_data_input = SourceIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            source_data_input.get(data).data_availability_);
  EXPECT_EQ("1234:5678:90ab:cdef:1234:5678:90ab:cdef", source_data_input.get(data).data_);

  auto destination_data_input = DestinationIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            destination_data_input.get(data).data_availability_);
  EXPECT_EQ("1234:5678:90ab:cdef:1234:5678:90ab:cdef", destination_data_input.get(data).data_);
}

// IP input matching data is not available.
TEST(IpDataInputBase, DataNotAvailable) {
  auto data = NetworkMatchingDataImpl(nullptr, nullptr);

  auto source_data_input = SourceIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::NotAvailable,
            source_data_input.get(data).data_availability_);
  EXPECT_EQ(absl::nullopt, source_data_input.get(data).data_);

  auto destination_data_input = DestinationIpDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::NotAvailable,
            destination_data_input.get(data).data_availability_);
  EXPECT_EQ(absl::nullopt, destination_data_input.get(data).data_);
}

// Returns corresponding port input matching data in network matching data.
TEST(PortDataInputBase, DataAvailable) {
  auto address = Address::Ipv4Instance("1.2.3.4", 5678);

  auto data = NetworkMatchingDataImpl(address.ip(), address.ip());

  auto source_data_input = SourcePortDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            source_data_input.get(data).data_availability_);
  EXPECT_EQ("5678", source_data_input.get(data).data_);

  auto destination_data_input = DestinationPortDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::AllDataAvailable,
            destination_data_input.get(data).data_availability_);
  EXPECT_EQ("5678", destination_data_input.get(data).data_);
}

// Port input matching data is not available.
TEST(PortDataInputBase, DataNotAvailable) {
  auto data = NetworkMatchingDataImpl(nullptr, nullptr);

  auto source_data_input = SourcePortDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::NotAvailable,
            source_data_input.get(data).data_availability_);
  EXPECT_EQ(absl::nullopt, source_data_input.get(data).data_);

  auto destination_data_input = DestinationPortDataInput();
  EXPECT_EQ(Matcher::DataInputGetResult::DataAvailability::NotAvailable,
            destination_data_input.get(data).data_availability_);
  EXPECT_EQ(absl::nullopt, destination_data_input.get(data).data_);
}

} // namespace
} // namespace Matching
} // namespace Network
} // namespace Envoy
