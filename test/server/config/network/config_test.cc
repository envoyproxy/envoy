#include <string>

#include "envoy/registry/registry.h"

#include "common/access_log/access_log_impl.h"
#include "common/config/filter_json.h"
#include "common/config/well_known_names.h"
#include "common/dynamo/dynamo_filter.h"
#include "common/protobuf/utility.h"

#include "server/config/access_log/file_access_log.h"
#include "server/config/network/client_ssl_auth.h"
#include "server/config/network/ext_authz.h"
#include "server/config/network/http_connection_manager.h"
#include "server/config/network/mongo_proxy.h"
#include "server/config/network/ratelimit.h"
#include "server/config/network/redis_proxy.h"
#include "server/config/network/tcp_proxy.h"

#include "test/mocks/grpc/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::_;

namespace Envoy {
namespace Server {
namespace Configuration {

// Negative test for protoc-gen-validate constraints.
TEST(NetworkFilterConfigTest, ValidateFail) {
  NiceMock<MockFactoryContext> context;

  ClientSslAuthConfigFactory client_ssl_auth_factory;
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth client_ssl_auth_proto;
  HttpConnectionManagerFilterConfigFactory hcm_factory;
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager hcm_proto;
  MongoProxyFilterConfigFactory mongo_factory;
  envoy::config::filter::network::mongo_proxy::v2::MongoProxy mongo_proto;
  RateLimitConfigFactory rate_limit_factory;
  envoy::config::filter::network::rate_limit::v2::RateLimit rate_limit_proto;
  RedisProxyFilterConfigFactory redis_factory;
  envoy::config::filter::network::redis_proxy::v2::RedisProxy redis_proto;
  TcpProxyConfigFactory tcp_proxy_factory;
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_proxy_proto;
  ExtAuthzConfigFactory ext_authz_factory;
  envoy::config::filter::network::ext_authz::v2::ExtAuthz ext_authz_proto;
  const std::vector<std::pair<NamedNetworkFilterConfigFactory&, Protobuf::Message&>> filter_cases =
      {
          {client_ssl_auth_factory, client_ssl_auth_proto},
          {ext_authz_factory, ext_authz_proto},
          {hcm_factory, hcm_proto},
          {mongo_factory, mongo_proto},
          {rate_limit_factory, rate_limit_proto},
          {redis_factory, redis_proto},
          {tcp_proxy_factory, tcp_proxy_proto},
      };

  for (const auto& filter_case : filter_cases) {
    EXPECT_THROW(filter_case.first.createFilterFactoryFromProto(filter_case.second, context),
                 ProtoValidationException);
  }

  EXPECT_THROW(FileAccessLogFactory().createAccessLogInstance(
                   envoy::config::filter::accesslog::v2::FileAccessLog(), nullptr, context),
               ProtoValidationException);
}

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

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config{};
  Config::FilterJson::translateRedisProxy(*json_config, proto_config);
  NiceMock<MockFactoryContext> context;
  RedisProxyFilterConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, RedisProxyEmptyProto) {
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
  envoy::config::filter::network::redis_proxy::v2::RedisProxy proto_config =
      *dynamic_cast<envoy::config::filter::network::redis_proxy::v2::RedisProxy*>(
          factory.createEmptyConfigProto().get());

  Config::FilterJson::translateRedisProxy(*json_config, proto_config);

  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
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
  NiceMock<MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactory(*json_config, context);
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
  NiceMock<MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
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
  NiceMock<MockFactoryContext> context;
  ClientSslAuthConfigFactory factory;
  envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth proto_config =
      *dynamic_cast<envoy::config::filter::network::client_ssl_auth::v2::ClientSSLAuth*>(
          factory.createEmptyConfigProto().get());

  Envoy::Config::FilterJson::translateClientSslAuthFilter(*json_config, proto_config);
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, RatelimitCorrectJson) {
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

TEST(NetworkFilterConfigTest, RatelimitCorrectProto) {
  std::string json_string = R"EOF(
  {
    "stat_prefix": "my_stat_prefix",
    "domain" : "fake_domain",
    "descriptors": [[{ "key" : "my_key",  "value" : "my_value" }]],
    "timeout_ms": 1337
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config{};
  Config::FilterJson::translateTcpRateLimitFilter(*json_config, proto_config);

  NiceMock<MockFactoryContext> context;
  RateLimitConfigFactory factory;
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, RatelimitEmptyProto) {
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
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config =
      *dynamic_cast<envoy::config::filter::network::rate_limit::v2::RateLimit*>(
          factory.createEmptyConfigProto().get());
  Config::FilterJson::translateTcpRateLimitFilter(*json_config, proto_config);

  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
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

  envoy::config::filter::accesslog::v2::FileAccessLog file_access_log;
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
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy config =
      *dynamic_cast<envoy::config::filter::network::tcp_proxy::v2::TcpProxy*>(
          factory.createEmptyConfigProto().get());
  config.set_stat_prefix("prefix");
  config.set_cluster("cluster");

  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

TEST(NetworkFilterConfigTest, ExtAuthzCorrectProto) {
  std::string json = R"EOF(
    {
      "grpc_service": {
          "google_grpc": {
             "target_uri": "ext_authz_server",
             "stat_prefix": "google"
           }
      },
      "failure_mode_allow": false,
      "stat_prefix": "name"
    }
    )EOF";

  envoy::config::filter::network::ext_authz::v2::ExtAuthz proto_config{};
  MessageUtil::loadFromJson(json, proto_config);

  NiceMock<MockFactoryContext> context;
  ExtAuthzConfigFactory factory;

  EXPECT_CALL(context.cluster_manager_.async_client_manager_, factoryForGrpcService(_, _))
      .WillOnce(Invoke([](const envoy::api::v2::core::GrpcService&, Stats::Scope&) {
        return std::make_unique<NiceMock<Grpc::MockAsyncClientFactory>>();
      }));
  NetworkFilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, context);
  Network::MockConnection connection;
  EXPECT_CALL(connection, addReadFilter(_));
  cb(connection);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
