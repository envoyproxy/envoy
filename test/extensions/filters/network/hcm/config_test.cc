#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/http/date_provider_impl.h"
#include "common/router/rds_impl.h"

#include "extensions/filters/network/hcm/config.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace HttpConnectionManager {

envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
parseHttpConnectionManagerFromJson(const std::string& json_string) {
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
      http_connection_manager;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateHttpConnectionManager(*json_object_ptr, http_connection_manager);
  return http_connection_manager;
}

class HttpConnectionManagerConfigTest : public testing::Test {
public:
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  Http::SlowDateProviderImpl date_provider_;
  Router::RouteConfigProviderManagerImpl route_config_provider_manager_{
      context_.runtime(),   context_.dispatcher(),  context_.random(),
      context_.localInfo(), context_.threadLocal(), context_.admin()};
};

TEST_F(HttpConnectionManagerConfigTest, InvalidFilterName) {
  const std::string json_string = R"EOF(
  {
    "codec_type": "http1",
    "stat_prefix": "router",
    "route_config":
    {
      "virtual_hosts": [
        {
          "name": "service",
          "domains": [ "*" ],
          "routes": [
            {
              "prefix": "/",
              "cluster": "cluster"
            }
          ]
        }
      ]
    },
    "filters": [
      { "name": "foo", "config": {} }
    ]
  }
  )EOF";

  EXPECT_THROW_WITH_MESSAGE(
      HttpConnectionManagerConfig(parseHttpConnectionManagerFromJson(json_string), context_,
                                  date_provider_, route_config_provider_manager_),
      EnvoyException, "Didn't find a registered implementation for name: 'foo'");
}

TEST_F(HttpConnectionManagerConfigTest, MiscConfig) {
  const std::string json_string = R"EOF(
  {
    "codec_type": "http1",
    "server_name": "foo",
    "stat_prefix": "router",
    "route_config":
    {
      "virtual_hosts": [
        {
          "name": "service",
          "domains": [ "*" ],
          "routes": [
            {
              "prefix": "/",
              "cluster": "cluster"
            }
          ]
        }
      ]
    },
    "tracing": {
      "operation_name": "ingress",
      "request_headers_for_tags": [ "foo" ]
    },
    "filters": [
      { "name": "http_dynamo_filter", "config": {} }
    ]
  }
  )EOF";

  HttpConnectionManagerConfig config(parseHttpConnectionManagerFromJson(json_string), context_,
                                     date_provider_, route_config_provider_manager_);

  EXPECT_THAT(std::vector<Http::LowerCaseString>({Http::LowerCaseString("foo")}),
              ContainerEq(config.tracingConfig()->request_headers_for_tags_));
  EXPECT_EQ(*context_.local_info_.address_, config.localAddress());
  EXPECT_EQ("foo", config.serverName());
}

TEST_F(HttpConnectionManagerConfigTest, SingleDateProvider) {
  const std::string json_string = R"EOF(
  {
    "codec_type": "http1",
    "stat_prefix": "router",
    "route_config":
    {
      "virtual_hosts": [
        {
          "name": "service",
          "domains": [ "*" ],
          "routes": [
            {
              "prefix": "/",
              "cluster": "cluster"
            }
          ]
        }
      ]
    },
    "filters": [
      { "name": "http_dynamo_filter", "config": {} }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerFilterConfigFactory factory;
  // We expect a single slot allocation vs. multiple.
  EXPECT_CALL(context_.thread_local_, allocateSlot());
  Server::Configuration::NetworkFilterFactoryCb cb1 =
      factory.createFilterFactory(*json_config, context_);
  Server::Configuration::NetworkFilterFactoryCb cb2 =
      factory.createFilterFactory(*json_config, context_);
}

TEST(HttpConnectionManagerConfigUtilityTest, DetermineNextProtocol) {
  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return("hello"));
    Buffer::OwnedImpl data("");
    EXPECT_EQ("hello", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("");
    EXPECT_EQ("", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("GET / HTTP/1.1");
    EXPECT_EQ("", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/2.0\r\n");
    EXPECT_EQ("h2", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/2");
    EXPECT_EQ("h2", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }

  {
    Network::MockConnection connection;
    EXPECT_CALL(connection, nextProtocol()).WillRepeatedly(Return(""));
    Buffer::OwnedImpl data("PRI * HTTP/");
    EXPECT_EQ("", HttpConnectionManagerConfigUtility::determineNextProtocol(connection, data));
  }
}

TEST_F(HttpConnectionManagerConfigTest, BadHttpConnectionMangerConfig) {
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
  EXPECT_THROW(factory.createFilterFactory(*json_config, context_), Json::Exception);
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogConfig) {
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
  EXPECT_THROW(factory.createFilterFactory(*json_config, context_), Json::Exception);
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogType) {
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
  EXPECT_THROW(factory.createFilterFactory(*json_config, context_), Json::Exception);
}

TEST_F(HttpConnectionManagerConfigTest, BadAccessLogNestedTypes) {
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
  EXPECT_THROW(factory.createFilterFactory(*json_config, context_), Json::Exception);
}

} // namespace HttpConnectionManager
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
