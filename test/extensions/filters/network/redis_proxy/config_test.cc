#include "common/config/filter_json.h"

#include "extensions/filters/network/redis_proxy/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyCorrectJson) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": {
      "op_timeout_ms": 20
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyCorrectProto) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": {
      "op_timeout_ms": 20
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config{};
  Config::FilterJson::translateRedisProxy(*json_config, proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(RedisProxyFilterConfigFactoryTest, RedisProxyEmptyProto) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": {
      "op_timeout_ms": 20
    }
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      *dynamic_cast<envoy::config::filter::network::redis_proxy::v2::RedisProxy*>(
          factory.createEmptyConfigProto().get());

  Config::FilterJson::translateRedisProxy(*json_config, proto_config);

  Server::Configuration::NetworkFilterFactoryCb cb =
      factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
