#include "source/common/network/utility.h"
#include "source/extensions/quic/server_preferred_address/datasource_server_preferred_address_config.h"

#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class DataSourceServerPreferredAddressConfigTest : public ::testing::Test {
public:
  DataSourceServerPreferredAddressConfigFactory factory_;
  testing::NiceMock<ProtobufMessage::MockValidationVisitor> visitor_;
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

TEST_F(DataSourceServerPreferredAddressConfigTest, Validation) {
  {
    // Bad address.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("not an address");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException, ".*bad v4 server preferred address: not an address.*");
  }
  {
    // Bad port.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_port()->set_inline_string("1000000"); // Out of range.
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string("127.0.0.1");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException,
                            ".*server preferred address v4 port was not a valid port: 1000000.*");
  }
  {
    // Cannot set dnat address but not spa address.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string("127.0.0.1");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException,
                            ".*AddressFamilyConfigValidationError.Address: value is required.*");
  }
  {
    // Cannot set port on address if dnat address isn't set.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_port()->set_inline_string("1");
    EXPECT_THROW_WITH_REGEX(
        factory_.createServerPreferredAddressConfig(cfg, visitor_, context_), EnvoyException,
        ".*port must be unset unless 'dnat_address' is set for address family v4.*");
  }
  {
    // v6 address in v4 field.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("::1");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException,
                            ".*wrong address family for v4 server preferred address.*");
  }
  {
    // v4 address in v6 field.
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv6_config()->mutable_address()->set_inline_string("127.0.0.1");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException,
                            ".*wrong address family for v6 server preferred address.*");
  }

  // Cannot read address.
  {
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_environment_variable(
        "does_not_exist_env_var");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException, ".*Environment variable doesn't exist.*");
  }

  // Cannot read port.
  {
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_port()->set_environment_variable("does_not_exist_env_var");
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string("127.0.0.1");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException, ".*Environment variable doesn't exist.*");
  }

  // Cannot read dnat_address.
  {
    envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig
        cfg;
    cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("127.0.0.1");
    cfg.mutable_ipv4_config()->mutable_dnat_address()->set_environment_variable(
        "does_not_exist_env_var");
    EXPECT_THROW_WITH_REGEX(factory_.createServerPreferredAddressConfig(cfg, visitor_, context_),
                            EnvoyException, ".*Environment variable doesn't exist.*");
  }
}

TEST_F(DataSourceServerPreferredAddressConfigTest, AddressAndPortIgnoresListenerPort) {
  envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("1.2.3.4");
  cfg.mutable_ipv4_config()->mutable_port()->set_inline_string("5");
  cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string("127.0.0.1");
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, context_);
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:5");
}

TEST_F(DataSourceServerPreferredAddressConfigTest, AddressAndUnsetPortUsesListenerPort) {
  envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("1.2.3.4");
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, context_);
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:1234");
}

TEST_F(DataSourceServerPreferredAddressConfigTest, DnatAddressAndUnsetPortUsesListenerPort) {
  envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_inline_string("1.2.3.4");
  cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string("127.0.0.1");
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, context_);
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.dnat_ipv4_.ToString(), "127.0.0.1:1234");
}

// Oftentimes files have a newline at the end, so accept data with whitespace around it.
TEST_F(DataSourceServerPreferredAddressConfigTest, NewlinesAccepted) {
  envoy::extensions::quic::server_preferred_address::v3::DataSourceServerPreferredAddressConfig cfg;
  cfg.mutable_ipv4_config()->mutable_address()->set_inline_string(" 1.2.3.4\n");
  cfg.mutable_ipv4_config()->mutable_port()->set_inline_string(" 443\n");
  cfg.mutable_ipv4_config()->mutable_dnat_address()->set_inline_string(" 127.0.0.1\n");
  auto obj = factory_.createServerPreferredAddressConfig(cfg, visitor_, context_);
  auto addresses = obj->getServerPreferredAddresses(
      Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 1234));
  EXPECT_EQ(addresses.ipv4_.ToString(), "1.2.3.4:443");
  EXPECT_EQ(addresses.dnat_ipv4_.ToString(), "127.0.0.1:1234");
}

} // namespace Quic
} // namespace Envoy
