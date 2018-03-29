#include "common/config/filter_json.h"

#include "extensions/filters/network/client_ssl_auth/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ClientSslAuth {

class IpWhiteListConfigTest : public ::testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_CASE_P(IpList, IpWhiteListConfigTest,
                        ::testing::Values(R"EOF(["192.168.3.0/24"])EOF",
                                          R"EOF(["2001:abcd::/64"])EOF"));

TEST_P(IpWhiteListConfigTest, ClientSslAuthCorrectJson) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list":)EOF" + GetParam() +
                            R"EOF(
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpWhiteListConfigTest, ClientSslAuthCorrectProto) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list":)EOF" + GetParam() +
                            R"EOF(
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config{};
  Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST_P(IpWhiteListConfigTest, ClientSslAuthEmptyProto) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list":)EOF" + GetParam() +
                            R"EOF(
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config =
      *dynamic_cast<envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth*>(
          factory.createEmptyConfigProto().get());

  Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config);
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace ClientSslAuth
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
