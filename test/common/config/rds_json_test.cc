#include "envoy/common/exception.h"

#include "common/config/rds_json.h"
#include "common/json/json_loader.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Config {
namespace {

TEST(RdsJsonTest, TestRuntimeFractionTranslation) {
  const std::string json_string = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "cluster": "www2",
    "runtime": {
      "default": 42,
      "key": "some_key"
    },
    "request_headers_to_add": [
      {
        "key": "x-key",
        "value": "%UPSTREAM_METADATA([\"namespace\", \"key\"])%"
      }
    ]
  }
  )EOF";
  envoy::api::v2::route::Route route;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRoute(*json_object_ptr, route);

  EXPECT_EQ(route.match().runtime_fraction().default_value().numerator(), 42);
  EXPECT_EQ(route.match().runtime_fraction().default_value().denominator(),
            envoy::type::FractionalPercent::HUNDRED);
  EXPECT_EQ(route.match().runtime_fraction().runtime_key(), "some_key");
}

TEST(RdsJsonTest, TestWeightedClusterTranslation) {
  const std::string json_string = R"EOF(
  {
    "prefix": "/new_endpoint",
    "prefix_rewrite": "/api/new_endpoint",
    "weighted_clusters": {
      "clusters": [
        {
          "name": "foo",
          "weight": 80
        },
        {
          "name": "bar",
          "weight": 20
        }
      ]
    }
  }
  )EOF";
  envoy::api::v2::route::Route route;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateRoute(*json_object_ptr, route);

  EXPECT_TRUE(route.has_route());
  EXPECT_TRUE(route.route().has_weighted_clusters());
  EXPECT_EQ(2, route.route().weighted_clusters().clusters_size());
  EXPECT_EQ("foo", route.route().weighted_clusters().clusters(0).name());
  EXPECT_EQ(80, route.route().weighted_clusters().clusters(0).weight().value());
  EXPECT_EQ("bar", route.route().weighted_clusters().clusters(1).name());
  EXPECT_EQ(20, route.route().weighted_clusters().clusters(1).weight().value());
}

TEST(RdsJsonTest, TestVirtualClusterTranslation) {
  const std::string json_string = R"EOF(
  {
    "name": "some-name",
    "method": "GET",
    "pattern": "/rides/d+"
  }
  )EOF";
  envoy::api::v2::route::VirtualCluster virtual_cluster;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateVirtualCluster(*json_object_ptr, virtual_cluster);

  EXPECT_EQ(envoy::api::v2::core::GET, virtual_cluster.method());
  EXPECT_EQ("some-name", virtual_cluster.name());
  EXPECT_EQ("/rides/d+", virtual_cluster.pattern());
}

TEST(RdsJsonTest, TestCorsTranslation) {
  const std::string json_string = R"EOF(
  {
    "allow_origin": ["www", "shop"],
    "allow_methods": "PUT",
    "allow_headers": "Content-Type",
    "expose_headers": "Content-Length",
    "max_age": "600",
    "allow_credentials": true,
    "enabled": true
  }
  )EOF";

  envoy::api::v2::route::CorsPolicy cors_policy;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::RdsJson::translateCors(*json_object_ptr, cors_policy);

  EXPECT_EQ("www", cors_policy.allow_origin().Get(0));
  EXPECT_EQ("shop", cors_policy.allow_origin().Get(1));
  EXPECT_EQ("PUT", cors_policy.allow_methods());
  EXPECT_EQ("Content-Type", cors_policy.allow_headers());
  EXPECT_EQ("Content-Length", cors_policy.expose_headers());
  EXPECT_EQ("600", cors_policy.max_age());
  EXPECT_EQ(true, cors_policy.allow_credentials().value());
  EXPECT_EQ(true, cors_policy.enabled().value());
}

} // namespace
} // namespace Config
} // namespace Envoy
