#include "envoy/common/exception.h"
#include "envoy/config/filter/http/router/v2/router.pb.h"
#include "envoy/config/filter/network/tcp_proxy/v2/tcp_proxy.pb.h"

#include "common/config/filter_json.h"
#include "common/json/json_loader.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

envoy::config::filter::http::router::v2::Router
parseRouterFromJson(const std::string& json_string) {
  envoy::config::filter::http::router::v2::Router router;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateRouter(*json_object_ptr, router);
  return router;
}

envoy::config::filter::network::tcp_proxy::v2::TcpProxy
parseTcpProxyFromJson(const std::string& json_string) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateTcpProxy(*json_object_ptr, tcp_proxy);
  return tcp_proxy;
}

} // namespace

TEST(FilterJsonTest, TranslateRouter) {
  std::string json_string = R"EOF(
    {
      "dynamic_stats": false,
      "start_child_span": true
    }
  )EOF";

  auto router = parseRouterFromJson(json_string);
  EXPECT_FALSE(router.dynamic_stats().value());
  EXPECT_TRUE(router.start_child_span());
}

TEST(FilterJsonTest, TranslateRouterDefaults) {
  std::string json_string = "{}";
  auto router = parseRouterFromJson(json_string);
  EXPECT_TRUE(router.dynamic_stats().value());
  EXPECT_FALSE(router.start_child_span());
}

TEST(FilterJsonTest, TranslateTcpProxyEmptyConfig) {
  // Simulates what happens when deprecated_v1 = true, but no value key is given.
  EXPECT_THROW_WITH_REGEX(parseTcpProxyFromJson("{}"), EnvoyException,
                          ".*deprecated_v1.*requires a value field.*");
}

TEST(FilterJsonTest, TranslateTcpProxy) {
  std::string json_string = R"EOF(
    {
      "stat_prefix": "stats",
      "route_config": {
        "routes": [
          {
            "cluster": "cluster1"
          },
          {
            "cluster": "cluster2",
            "source_ip_list": [ "127.0.0.1/8" ],
            "source_ports": "1000",
            "destination_ip_list": [ "10.10.0.1/16" ],
            "destination_ports": "2000"
          }
        ]
      },
      "access_log": [
        {
          "path": "/dev/null"
        }
      ]
    }
  )EOF";

  auto tcp_proxy = parseTcpProxyFromJson(json_string);
  EXPECT_EQ("stats", tcp_proxy.stat_prefix());
  EXPECT_TRUE(tcp_proxy.has_deprecated_v1());
  auto routes = tcp_proxy.deprecated_v1().routes();
  EXPECT_EQ(2, routes.size());
  EXPECT_EQ("cluster1", routes[0].cluster());
  EXPECT_EQ("cluster2", routes[1].cluster());
  EXPECT_EQ("127.0.0.0", routes[1].source_ip_list()[0].address_prefix());
  EXPECT_EQ(8, routes[1].source_ip_list()[0].prefix_len().value());
  EXPECT_EQ("1000", routes[1].source_ports());
  EXPECT_EQ("10.10.0.0", routes[1].destination_ip_list()[0].address_prefix());
  EXPECT_EQ(16, routes[1].destination_ip_list()[0].prefix_len().value());
  EXPECT_EQ("2000", routes[1].destination_ports());
  EXPECT_EQ(1, tcp_proxy.access_log().size());
}

} // namespace Config
} // namespace Envoy
