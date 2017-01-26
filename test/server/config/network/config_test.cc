#include "server/config/network/client_ssl_auth.h"
#include "server/config/network/mongo_proxy.h"
#include "server/config/network/ratelimit.h"
#include "server/config/network/redis_proxy.h"
#include "server/config/network/tcp_proxy.h"

#include "test/mocks/server/mocks.h"

using testing::_;
using testing::NiceMock;

namespace Server {
namespace Configuration {

TEST(NetworkFilterConfigTest, RedisProxy) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  RedisProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.tryCreateFilterFactory(NetworkFilterType::Read, "redis_proxy", *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, MongoProxy) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  MongoProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.tryCreateFilterFactory(NetworkFilterType::Both, "mongo_proxy", *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, BadMongoProxyConfig) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "access_log" : "path/to/access/log",
    "test" : "a"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  MongoProxyFilterConfigFactory factory;
  EXPECT_THROW(
      factory.tryCreateFilterFactory(NetworkFilterType::Both, "mongo_proxy", *json_config, server),
      Json::Exception);
}

TEST(NetworkFilterConfigTest, TcpProxy) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "cluster" : "fake_cluster"
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  TcpProxyConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.tryCreateFilterFactory(NetworkFilterType::Read, "tcp_proxy", *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, ClientSslAuth) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list": ["192.168.3.0/24"]
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  ClientSslAuthConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.tryCreateFilterFactory(
      NetworkFilterType::Read, "client_ssl_auth", *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, Ratelimit) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]]
  }
  )EOF";

  Json::ObjectPtr json_config = Json::Factory::LoadFromString(json_string);
  NiceMock<MockInstance> server;
  RateLimitConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.tryCreateFilterFactory(NetworkFilterType::Read, "ratelimit", *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // Configuration
} // Server
