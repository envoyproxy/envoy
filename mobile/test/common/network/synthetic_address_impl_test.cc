#include "gtest/gtest.h"
#include "library/common/network/synthetic_address_impl.h"

namespace Envoy {
namespace Network {
namespace Address {

TEST(SyntheticAddressImplTest, AllAddressesAreNotEqual) {
  SyntheticAddressImpl address1;
  SyntheticAddressImpl address2;
  ASSERT_NE(address1, address2);
}

TEST(SyntheticAddressImplTest, Names) {
  SyntheticAddressImpl address;
  ASSERT_EQ(address.asString(), "synthetic");
  ASSERT_EQ(address.asStringView(), "synthetic");
  ASSERT_EQ(address.logicalName(), "synthetic");
}

TEST(SyntheticAddressImplTest, Accessors) {
  SyntheticAddressImpl address;
  ASSERT_EQ(address.ip(), nullptr);
  ASSERT_EQ(address.pipe(), nullptr);
  ASSERT_EQ(address.sockAddr(), nullptr);
  ASSERT_EQ(address.sockAddrLen(), 0);
}

TEST(SyntheticAddressImplTest, Type) {
  SyntheticAddressImpl address;
  ASSERT_EQ(address.type(), Type::Ip);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
