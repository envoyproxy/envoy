#include "common/buffer/buffer_impl.h"
#include "common/http/date_provider_impl.h"

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

class HttpConnectionManagerConfigTest : public testing::Test {
public:
  NiceMock<MockFactoryContext> context_;
  Http::SlowDateProviderImpl date_provider_;
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
      { "type": "encoder", "name": "foo", "config": {}
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  EXPECT_THROW_WITH_MESSAGE(HttpConnectionManagerConfig(*json_config, context_, date_provider_),
                            EnvoyException,
                            "unable to create http filter factory for 'foo'/'encoder'");
}

TEST_F(HttpConnectionManagerConfigTest, InvalidFilterType) {
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
      { "type": "encoder", "name": "router", "config": {}
      }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  EXPECT_THROW_WITH_MESSAGE(HttpConnectionManagerConfig(*json_config, context_, date_provider_),
                            EnvoyException,
                            "unable to create http filter factory for 'router'/'encoder'");
}

TEST_F(HttpConnectionManagerConfigTest, MiscConfig) {
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
    "tracing": {
      "operation_name": "ingress",
      "request_headers_for_tags": [ "foo" ]
    },
    "filters": [
      { "type": "both", "name": "http_dynamo_filter", "config": {} }
    ]
  }
  )EOF";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  HttpConnectionManagerConfig config(*json_config, context_, date_provider_);
  EXPECT_THAT(std::vector<Http::LowerCaseString>({Http::LowerCaseString("foo")}),
              ContainerEq(config.tracingConfig()->request_headers_for_tags_));
  EXPECT_EQ(*context_.local_info_.address_, config.localAddress());
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
      { "type": "both", "name": "http_dynamo_filter", "config": {} }
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

} // namespace Configuration
} // namespace Server
} // namespace Envoy
