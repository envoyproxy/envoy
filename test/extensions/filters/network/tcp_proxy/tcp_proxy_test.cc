#include <cstdint>
#include <memory>
#include <string>

#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/network/address_impl.h"
#include "common/stats/stats_impl.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/filters/network/tcp_proxy/tcp_proxy.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::MatchesRegex;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpProxy {

namespace {
TcpProxyConfig constructTcpProxyConfigFromJson(const Json::Object& json,
                                               Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy;
  Config::FilterJson::translateTcpProxy(json, tcp_proxy);
  return TcpProxyConfig(tcp_proxy, context);
}
} // namespace

TEST(TcpProxyConfigTest, NoRouteConfig) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name"
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructTcpProxyConfigFromJson(*config, factory_context), EnvoyException);
}

TEST(TcpProxyConfigTest, BadTcpProxyConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": 1,
    "route_config": {
      "routes": [
        {
          "cluster": "fake_cluster"
        }
      ]
    }
   }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;
  EXPECT_THROW(constructTcpProxyConfigFromJson(*json_config, factory_context), Json::Exception);
}

TEST(TcpProxyConfigTest, Routes) {
  std::string json = R"EOF(
    {
      "stat_prefix": "name",
      "route_config": {
        "routes": [
          {
            "destination_ip_list": [
              "10.10.10.10/32",
              "10.10.11.0/24",
              "10.11.0.0/16",
              "11.0.0.0/8",
              "128.0.0.0/1"
            ],
            "cluster": "with_destination_ip_list"
          },
          {
            "destination_ip_list": [
              "::1/128",
              "2001:abcd::/64"
            ],
            "cluster": "with_v6_destination"
          },
          {
            "destination_ports": "1-1024,2048-4096,12345",
            "cluster": "with_destination_ports"
          },
          {
            "source_ports": "23457,23459",
            "cluster": "with_source_ports"
          },
          {
            "destination_ip_list": [
              "2002::/32"
            ],
            "source_ip_list": [
              "2003::/64"
            ],
            "cluster": "with_v6_source_and_destination"
          },
          {
            "destination_ip_list": [
              "10.0.0.0/24"
            ],
            "source_ip_list": [
              "20.0.0.0/24"
            ],
            "destination_ports" : "10000",
            "source_ports": "20000",
            "cluster": "with_everything"
          },
          {
            "cluster": "catch_all"
          }
        ]
      }
    }
    )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json);
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  TcpProxyConfig config_obj(constructTcpProxyConfigFromJson(*json_config, factory_context_));

  {
    // hit route with destination_ip (10.10.10.10/32)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.10.10");
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.10.11");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.10.11.0/24)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.10.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (10.11.0.0/16)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.11.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("10.12.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (11.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("11.11.11.11");
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // fall-through
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("12.12.12.12");
    connection.remote_address_ = std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
    EXPECT_EQ(std::string("catch_all"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination_ip (128.0.0.0/8)
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("128.255.255.255");
    EXPECT_EQ(std::string("with_destination_ip_list"), config_obj.getRouteFromEntries(connection));
  }

  {
    // hit route with destination port range
    NiceMock<Network::MockConnection> connection;
    connection.local_address_ = std::make_shared<Network::Address::Ipv4Instance>("1.2.3.4", 12345);
    EXPECT_EQ(std::string("with_destination_ports"), config_obj.getRouteFromEntries(connection));
  }
