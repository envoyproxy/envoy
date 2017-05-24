#include <string>

#include "common/dynamo/dynamo_filter.h"

#include "server/config/network/client_ssl_auth.h"
#include "server/config/network/http_connection_manager.h"
#include "server/config/network/mongo_proxy.h"
#include "server/config/network/ratelimit.h"
#include "server/config/network/redis_proxy.h"
#include "server/config/network/tcp_proxy.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::_;
using testing::NiceMock;

namespace Server {
namespace Configuration {

TEST(NetworkFilterConfigTest, RedisProxy) {
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
  NiceMock<MockInstance> server;
  RedisProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.createFilterFactory(NetworkFilterType::Read, *json_config, server);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  MongoProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.createFilterFactory(NetworkFilterType::Both, *json_config, server);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  MongoProxyFilterConfigFactory factory;
  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Both, *json_config, server),
               Json::Exception);
}

TEST(NetworkFilterConfigTest, TcpProxy) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "route_config": {
      "routes": [
        {
          "destination_ip_list": [
            "192.168.1.1/32",
            "192.168.1.0/24"
          ],
          "source_ip_list": [
            "192.168.0.0/16",
            "192.0.0.0/8",
            "127.0.0.0/8"
          ],
          "destination_ports": "1-1024,2048-4096,12345",
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
  NiceMock<MockInstance> server;
  TcpProxyConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.createFilterFactory(NetworkFilterType::Read, *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);

  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Both, *json_config, server),
               EnvoyException);
}

TEST(NetworkFilterConfigTest, ClientSslAuth) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list": ["192.168.3.0/24"]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  ClientSslAuthConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.createFilterFactory(NetworkFilterType::Read, *json_config, server);
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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  RateLimitConfigFactory factory;
  NetworkFilterFactoryCb cb =
      factory.createFilterFactory(NetworkFilterType::Read, *json_config, server);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, BadHttpConnectionMangerConfig) {
  std::string json_string = R"EOF(
  {
    "codec_type" : "http1",
    "stat_prefix" : "my_stat_prefix",
    "route_config" : {
      "virtual_hosts" : [
        {
          "name" : "default",
          "domains" : ["*"],
          "routes" : [
            {
              "prefix" : "/",
              "cluster": "fake_cluster"
            }
          ]
        }
      ]
    },
    "filter" : [{}]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerFilterConfigFactory factory;
  NiceMock<MockInstance> server;
  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Read, *json_config, server),
               Json::Exception);
}

TEST(NetworkFilterConfigTest, BadAccessLogConfig) {
  std::string json_string = R"EOF(
  {
    "codec_type" : "http1",
    "stat_prefix" : "my_stat_prefix",
    "route_config" : {
      "virtual_hosts" : [
        {
          "name" : "default",
          "domains" : ["*"],
          "routes" : [
            {
              "prefix" : "/",
              "cluster": "fake_cluster"
            }
          ]
        }
      ]
    },
    "filters" : [
      {
        "type" : "both",
        "name" : "http_dynamo_filter",
        "config" : {}
      }
    ],
    "access_log" :[
      {
        "path" : "mypath",
        "filter" : []
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerFilterConfigFactory factory;
  NiceMock<MockInstance> server;
  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Read, *json_config, server),
               Json::Exception);
}

TEST(NetworkFilterConfigTest, BadAccessLogType) {
  std::string json_string = R"EOF(
  {
    "codec_type" : "http1",
    "stat_prefix" : "my_stat_prefix",
    "route_config" : {
      "virtual_hosts" : [
        {
          "name" : "default",
          "domains" : ["*"],
          "routes" : [
            {
              "prefix" : "/",
              "cluster": "fake_cluster"
            }
          ]
        }
      ]
    },
    "filters" : [
      {
        "type" : "both",
        "name" : "http_dynamo_filter",
        "config" : {}
      }
    ],
    "access_log" :[
      {
        "path" : "mypath",
        "filter" : {
          "type" : "bad_type"
        }
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerFilterConfigFactory factory;
  NiceMock<MockInstance> server;
  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Read, *json_config, server),
               Json::Exception);
}

TEST(NetworkFilterConfigTest, BadAccessLogNestedTypes) {
  std::string json_string = R"EOF(
  {
    "codec_type" : "http1",
    "stat_prefix" : "my_stat_prefix",
    "route_config" : {
      "virtual_hosts" : [
        {
          "name" : "default",
          "domains" : ["*"],
          "routes" : [
            {
              "prefix" : "/",
              "cluster": "fake_cluster"
            }
          ]
        }
      ]
    },
    "filters" : [
      {
        "type" : "both",
        "name" : "http_dynamo_filter",
        "config" : {}
      }
    ],
    "access_log" :[
      {
        "path": "/dev/null",
        "filter": {
          "type": "logical_and",
          "filters": [
            {
              "type": "logical_or",
              "filters": [
                {"type": "duration", "op": ">=", "value": 10000},
                {"type": "bad_type"}
              ]
            },
            {"type": "not_healthcheck"}
          ]
        }
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerFilterConfigFactory factory;
  NiceMock<MockInstance> server;
  EXPECT_THROW(factory.createFilterFactory(NetworkFilterType::Read, *json_config, server),
               Json::Exception);
}

/**
 * Deprecated version of config registration for http dynamodb filter.
 */
class TestDeprecatedDynamoFilterConfig : public HttpFilterConfigFactory {
public:
  HttpFilterFactoryCb tryCreateFilterFactory(HttpFilterType type, const std::string& name,
                                             const Json::Object&, const std::string& stat_prefix,
                                             Server::Instance& server) override {
    if (type != HttpFilterType::Both || name != "http_dynamo_filter_deprecated") {
      return nullptr;
    }

    return [&server, stat_prefix](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamFilter(Http::StreamFilterSharedPtr{
          new Dynamo::DynamoFilter(server.runtime(), stat_prefix, server.stats())});
    };
  }
};

TEST(NetworkFilterConfigTest, DeprecatedHttpFilterConfigFactoryTest) {
  // Test just ensures that the deprecated http filter registration still works without error.

  // Register the config factory
  RegisterHttpFilterConfigFactory<TestDeprecatedDynamoFilterConfig> registered;

  std::string json = R"EOF(
  {
    "codec_type" : "http1",
    "stat_prefix" : "my_stat_prefix",
    "route_config" : {
      "virtual_hosts" : [
        {
          "name" : "default",
          "domains" : ["*"],
          "routes" : [
            {
              "prefix" : "/",
              "cluster": "fake_cluster"
            }
          ]
        }
      ]
    },
    "filters" : [
      {
        "type" : "both",
        "name" : "http_dynamo_filter_deprecated",
        "config" : {}
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  HttpConnectionManagerFilterConfigFactory factory;
  NiceMock<Server::MockInstance> server;
  factory.createFilterFactory(NetworkFilterType::Read, *loader, server);
}

TEST(NetworkFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(RegisterNamedNetworkFilterConfigFactory<ClientSslAuthConfigFactory>(),
                            EnvoyException, "Attempted to register multiple "
                                            "NamedNetworkFilterConfigFactory objects with name: "
                                            "'client_ssl_auth'");
}

} // Configuration
} // Server
} // Envoy
