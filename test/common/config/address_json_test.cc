#include "envoy/common/exception.h"

#include "common/config/address_json.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {

TEST(AddressJsonTest, TranslateResolvedUrlAddress) {
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("tcp://1.2.3.4:5678", /*url=*/true, /*resolved=*/true,
                                  proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kSocketAddress, proto_address.address_case());
    EXPECT_EQ(envoy::api::v2::core::SocketAddress::TCP, proto_address.socket_address().protocol());
    EXPECT_EQ("1.2.3.4", proto_address.socket_address().address());
    EXPECT_EQ(5678, proto_address.socket_address().port_value());
  }
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("udp://1.2.3.4:5678", /*url=*/true, /*resolved=*/true,
                                  proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kSocketAddress, proto_address.address_case());
    EXPECT_EQ(envoy::api::v2::core::SocketAddress::UDP, proto_address.socket_address().protocol());
    EXPECT_EQ("1.2.3.4", proto_address.socket_address().address());
    EXPECT_EQ(5678, proto_address.socket_address().port_value());
  }
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("unix://foo/bar", /*url=*/true, /*resolved=*/true, proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kPipe, proto_address.address_case());
    EXPECT_EQ("foo/bar", proto_address.pipe().path());
  }
  {
    envoy::api::v2::core::Address proto_address;
    EXPECT_THROW(AddressJson::translateAddress("invalid://1.2.3.4:5678", /*url=*/true,
                                               /*resolved=*/true, proto_address),
                 EnvoyException);
  }
}

TEST(AddressJsonTest, TranslateResolvedNonUrlAddress) {
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("1.2.3.4:5678", /*url=*/false, /*resolved=*/true, proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kSocketAddress, proto_address.address_case());
    EXPECT_EQ(envoy::api::v2::core::SocketAddress::TCP, proto_address.socket_address().protocol());
    EXPECT_EQ("1.2.3.4", proto_address.socket_address().address());
    EXPECT_EQ(5678, proto_address.socket_address().port_value());
  }
  {
    envoy::api::v2::core::Address proto_address;
    EXPECT_THROW(AddressJson::translateAddress("tcp://1.2.3.4:5678", /*url=*/false,
                                               /*resolved=*/true, proto_address),
                 EnvoyException);
  }
}

TEST(AddressJsonTest, TranslateUnresolvedUrlAddress) {
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("tcp://foo.com:5678", /*url=*/true, /*resolved=*/false,
                                  proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kSocketAddress, proto_address.address_case());
    EXPECT_EQ(envoy::api::v2::core::SocketAddress::TCP, proto_address.socket_address().protocol());
    EXPECT_EQ("foo.com", proto_address.socket_address().address());
    EXPECT_EQ(5678, proto_address.socket_address().port_value());
  }
  {
    envoy::api::v2::core::Address proto_address;
    AddressJson::translateAddress("udp://bar.com:5678", /*url=*/true, /*resolved=*/false,
                                  proto_address);
    EXPECT_EQ(envoy::api::v2::core::Address::kSocketAddress, proto_address.address_case());
    EXPECT_EQ(envoy::api::v2::core::SocketAddress::UDP, proto_address.socket_address().protocol());
    EXPECT_EQ("bar.com", proto_address.socket_address().address());
    EXPECT_EQ(5678, proto_address.socket_address().port_value());
  }
  {
    envoy::api::v2::core::Address proto_address;
    EXPECT_THROW(AddressJson::translateAddress("unix://foo/bar", /*url=*/true, /*resolved=*/false,
                                               proto_address),
                 EnvoyException);
  }
  {
    envoy::api::v2::core::Address proto_address;
    EXPECT_THROW(AddressJson::translateAddress("invalid://qux.com:5678", /*url=*/true,
                                               /*resolved=*/false, proto_address),
                 EnvoyException);
  }
}

} // namespace Config
} // namespace Envoy
