#include <string>

#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"
#include "common/config/well_known_names.h"
#include "common/dynamo/dynamo_filter.h"

#include "server/config/network/client_ssl_auth.h"
#include "server/config/network/file_access_log.h"
#include "server/config/network/http_connection_manager.h"
#include "server/config/network/ratelimit.h"
#include "server/config/network/redis_proxy.h"
#include "server/config/network/tcp_proxy.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(NetworkFilterConfigTest, RedisProxyCorrectJson) {
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
  NiceMock<MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, RedisProxyCorrectProto) {
  std::string json_string = R"EOF(
  {
    "cluster_name": "fake_cluster",
    "stat_prefix": "foo",
    "conn_pool": {
      "op_timeout_ms": 20
    }
  }
  )EOF";

  envoy::api::v2::filter::network::RedisProxy config{};
  config.set_cluster("fake_cluster");
  config.set_stat_prefix("foo");
  config.mutable_settings()->mutable_op_timeout()->set_seconds(1);

  NiceMock<MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, RedisProxyEmptyProto) {
  NiceMock<MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  envoy::api::v2::filter::network::RedisProxy config =
      *dynamic_cast<envoy::api::v2::filter::network::RedisProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_cluster("fake_cluster");
  config.set_stat_prefix("foo");
  config.mutable_settings()->mutable_op_timeout()->set_seconds(1);

  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

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
  NiceMock<MockFactoryContext> context;
  TcpProxyConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);

  factory.createFilterFactory(*json_config, context);
}

class IpWhiteListConfigTest : public ::testing::TestWithParam<std::string> {};

INSTANTIATE_TEST_CASE_P(IpList, IpWhiteListConfigTest,
                        ::testing::Values(R"EOF(["192.168.3.0/24"])EOF",
                                          R"EOF(["2001:abcd::/64"])EOF"));

TEST_P(IpWhiteListConfigTest, ClientSslAuth) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "auth_api_cluster" : "fake_cluster",
    "ip_white_list":)EOF" + GetParam() +
                            R"EOF(
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, Ratelimit) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]],
    "timeout_ms": 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockFactoryContext> context;
  RateLimitConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
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
  NiceMock<MockFactoryContext> context;
  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
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
  NiceMock<MockFactoryContext> context;
  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
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
  NiceMock<MockFactoryContext> context;
  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
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
  NiceMock<MockFactoryContext> context;
  EXPECT_THROW(factory.createFilterFactory(*json_config, context), Json::Exception);
}

TEST(NetworkFilterConfigTest, DoubleRegistrationTest) {
  EXPECT_THROW_WITH_MESSAGE(
      (Registry::RegisterFactory<ClientSslAuthConfigFactory, NamedNetworkFilterConfigFactory>()),
      EnvoyException,
      fmt::format("Double registration for name: '{}'",
                  Config::NetworkFilterNames::get().CLIENT_SSL_AUTH));
}

TEST(AccessLogConfigTest, FileAccessLogTest) {
  auto factory = Registry::FactoryRegistry<AccessLogInstanceFactory>::getFactory(
      Config::AccessLogNames::get().FILE);
  ASSERT_NE(nullptr, factory);

  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  ASSERT_NE(nullptr, message);

  envoy::api::v2::filter::accesslog::FileAccessLog file_access_log;
  file_access_log.set_path("/dev/null");
  file_access_log.set_format("%START_TIME%");
  MessageUtil::jsonConvert(file_access_log, *message);

  AccessLog::FilterPtr filter;
  NiceMock<Server::Configuration::MockFactoryContext> context;

  AccessLog::InstanceSharedPtr instance =
      factory->createAccessLogInstance(*message, std::move(filter), context);
  EXPECT_NE(nullptr, instance);
  EXPECT_NE(nullptr, dynamic_cast<AccessLog::FileAccessLog*>(instance.get()));
}

// Test that a minimal TcpProxy v2 config works.
TEST(TcpProxyConfigTest, TcpProxyConfigTest) {
  NiceMock<MockFactoryContext> context;
  TcpProxyConfigFactory factory;
  envoy::api::v2::filter::network::TcpProxy config =
      *dynamic_cast<envoy::api::v2::filter::network::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");

  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
