#include "common/buffer/buffer_impl.h"
#include "common/config/filter_json.h"
#include "common/http/date_provider_impl.h"
#include "common/router/rds_impl.h"

#include "server/config/network/http_connection_manager.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ContainerEq;
using testing::Return;

namespace Envoy {
namespace Server {
namespace Configuration {
namespace {

envoy::api::v2::filter::network::HttpConnectionManager
parseHttpConnectionManagerFromJson(const std::string& json_string) {
  envoy::api::v2::filter::network::HttpConnectionManager http_connection_manager;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::FilterJson::translateHttpConnectionManager(*json_object_ptr, http_connection_manager);
  return http_connection_manager;
}

class HttpConnectionManagerConfigTest : public testing::Test {
public:
  NiceMock<MockFactoryContext> context_;
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
  NetworkFilterFactoryCb cb1 = factory.createFilterFactory(*json_config, context_);
  NetworkFilterFactoryCb cb2 = factory.createFilterFactory(*json_config, context_);
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

} // namespace
} // namespace Configuration
} // namespace Server
} // namespace Envoy
