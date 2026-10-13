#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/network/well_known_names.h"

#include "test/mocks/server/factory_context.h"

#include "contrib/client_ssl_auth/filters/network/source/config.h"
#include "contrib/envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.h"
#include "contrib/envoy/extensions/filters/network/client_ssl_auth/v3/client_ssl_auth.pb.validate.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

class IpAllowListConfigTest : public testing::TestWithParam<std::string> {};

const std::string ipv4_cidr_yaml = R"EOF(
- address_prefix: "192.168.3.0"
  prefix_len: 24
)EOF";

const std::string ipv6_cidr_yaml = R"EOF(
- address_prefix: "2001:abcd::"
  prefix_len: 64
)EOF";

INSTANTIATE_TEST_SUITE_P(IpList, IpAllowListConfigTest,
                         ::testing::Values(ipv4_cidr_yaml, ipv6_cidr_yaml));

TEST_P(IpAllowListConfigTest, ClientSslAuthCorrectJson) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  context.server_factory_context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  ClientSslAuthConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpAllowListConfigTest, ClientSslAuthCorrectProto) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  context.server_factory_context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  ClientSslAuthConfigFactory factory;
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpAllowListConfigTest, ClientSslAuthEmptyProto) {
  const std::string yaml = R"EOF(
stat_prefix: my_stat_prefix
auth_api_cluster: fake_cluster
ip_white_list:
)EOF" + GetParam();

  NiceMock<Server::Configuration::MockFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  context.server_factory_context_.cluster_manager_.initializeThreadLocalClusters({"fake_cluster"});
  ClientSslAuthConfigFactory factory;
  envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth proto_config =
      *dynamic_cast<envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth*>(
          factory.createEmptyConfigProto().get());

  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  Network::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context).value();
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(ClientSslAuthConfigFactoryTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      ClientSslAuthConfigFactory()
          .createFilterFactoryFromProto(
              envoy::extensions::filters::network::client_ssl_auth::v3::ClientSSLAuth(), context)
          .IgnoreError(),
      ProtoValidationException);
}

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
