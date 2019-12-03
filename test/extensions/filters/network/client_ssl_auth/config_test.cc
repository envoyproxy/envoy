#include "envoy/registry/registry.h"

#include "common/protobuf/utility.h"

#include "extensions/filters/network/client_ssl_auth/config.h"
#include "extensions/filters/network/well_known_names.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

class IpWhiteListConfigTest : public testing::TestWithParam<std::string> {};

const std::string ipv4_cidr_yaml = R"EOF(
- address_prefix: "192.168.3.0"
  prefix_len: 24
)EOF";

const std::string ipv6_cidr_yaml = R"EOF(
- address_prefix: "2001:abcd::"
  prefix_len: 64
)EOF";

INSTANTIATE_TEST_SUITE_P(IpList, IpWhiteListConfigTest,
                         ::testing::Values(ipv4_cidr_yaml, ipv6_cidr_yaml));

TEST_P(IpWhiteListConfigTest, ClientSslAuthCorrectJson) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpWhiteListConfigTest, ClientSslAuthCorrectProto) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpWhiteListConfigTest, ClientSslAuthEmptyProto) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config =
      *dynamic_cast<envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth*>(
          factory.createEmptyConfigProto().get());

  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(ClientSslAuthConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(ClientSslAuthConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth(), context),
               ProtoValidationException);
}

TEST(ClientSslAuthConfigFactoryTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ClientSslAuthConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'", NetworkFilterNames::get().ClientSslAuth));
}

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
