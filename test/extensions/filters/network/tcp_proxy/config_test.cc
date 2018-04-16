#include "extensions/filters/network/tcp_proxy/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

class RouteIpListConfigTest : public ::testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_CASE_P(IpList, RouteIpListConfigTest,
                        ::testing::Values(R"EOF("destination_ip_list": [
                                                  "192.168.1.1/32",
                                                  "192.168.1.0/24"
                                                ],
                                                "source_ip_list": [
                                                  "192.168.0.0/16",
                                                  "192.0.0.0/8",
                                                  "127.0.0.0/8"
                                                ],)EOF",
                                          R"EOF("destination_ip_list": [
                                                  "2001:abcd::/64",
                                                  "2002:ffff::/32"
                                                ],
                                                "source_ip_list": [
                                                  "ffee::/128",
                                                  "2001::abcd/64",
                                                  "1234::5678/128"
                                                ],)EOF"));

TEST_P(RouteIpListConfigTest, TcpProxy) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "route_config": {
      "routes": [
        {)EOF" + GetParam() +
                            R"EOF("destination_ports": "1-1024,2048-4096,12345",
          "cluster": "fake_cluster"
        },
        {
          "source_ports": "23457,23459",
          "cluster": "fake_cluster2"
        }
      ]
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TcpProxyConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);

  factory.createFilterFactory(*json_config, context);
}

TEST(TcpProxyConfigTest, ValidateFail) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(TcpProxyConfigFactory().createFilterFactoryFromProto(
                   envoy::config::filter::network::tcp_proxy::v2::TcpProxy(), context),
               ProtoValidationException);
}

// Test that a minimal TcpProxy v2 config works.
TEST(TcpProxyConfigTest, TcpProxyConfigTest) {
  NiceMock<Server::Configuration::MockFactoryContext> context;
  TcpProxyConfigFactory factory;
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy config =
      *dynamic_cast<envoy::config::filter::network::tcp_proxy::v2::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");

  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace TcpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
