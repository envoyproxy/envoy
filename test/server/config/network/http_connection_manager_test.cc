#include "common/buffer/buffer_impl.h"

#include "server/config/network/http_connection_manager.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::Return;

namespace Server {
namespace Configuration {

TEST(HttpConnectionManagerConfigTest, invalidFilter) {
  std::string json_string = R"EOF(
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
  NiceMock<MockFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(HttpConnectionManagerConfig(*json_config, context), EnvoyException,
                            "unable to create http filter factory for 'foo'/'encoder'");
}

TEST(HttpConnectionManagerConfigUtilityTest, determineNextProtocol) {
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

} // Configuration
} // Server
} // Envoy
