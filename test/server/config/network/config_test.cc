#include "server/config/network/redis_proxy.h"

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

} // Configuration
} // Server
