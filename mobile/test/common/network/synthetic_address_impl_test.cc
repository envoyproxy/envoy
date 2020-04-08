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

TEST(SyntheticAddressImplTest, Syscalls) {
  SyntheticAddressImpl address;

  const Api::SysCallIntResult bind_rc = address.bind(0);
  ASSERT_EQ(bind_rc.rc_, -1);
  ASSERT_EQ(bind_rc.errno_, EADDRNOTAVAIL);

  const Api::SysCallIntResult connect_rc = address.connect(0);
  ASSERT_EQ(connect_rc.rc_, -1);
  ASSERT_EQ(connect_rc.errno_, EPROTOTYPE);
}

TEST(SyntheticAddressImplTest, Accessors) {
  SyntheticAddressImpl address;
  ASSERT_EQ(address.ip(), nullptr);
  ASSERT_EQ(address.socket(SocketType::Datagram), nullptr);
  ASSERT_EQ(address.socket(SocketType::Stream), nullptr);
}

TEST(SyntheticAddressImplTest, Type) {
  SyntheticAddressImpl address;
  ASSERT_EQ(address.type(), Type::Ip);
}

} // namespace Address
} // namespace Network
} // namespace Envoy
