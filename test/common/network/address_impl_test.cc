#include "envoy/common/exception.h"

#include "common/network/address_impl.h"

namespace Network {
namespace Address {

TEST(Ipv4InstanceTest, Basic) {
  Ipv4Instance address("127.0.0.1", 80);
  EXPECT_EQ("127.0.0.1:80", address.asString());
  EXPECT_EQ(Type::Ip, address.type());
  EXPECT_EQ("127.0.0.1", address.ip()->addressAsString());
  EXPECT_EQ(80U, address.ip()->port());
  EXPECT_EQ(IpVersion::v4, address.ip()->version());
}

TEST(Ipv4InstanceTest, BadAddress) { EXPECT_THROW(Ipv4Instance("foo"), EnvoyException); }

TEST(PipeInstanceTest, Basic) {
  PipeInstance address("/foo");
  EXPECT_EQ("/foo", address.asString());
  EXPECT_EQ(Type::Pipe, address.type());
  EXPECT_EQ(nullptr, address.ip());
}

} // Address
} // Network
