#include <string>

#include "envoy/registry/registry.h"

#include "server/config/stats/tcp_statsd.h"
#include "server/config/stats/udp_statsd.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Server {
namespace Configuration {

TEST(StatsConfigTest, ValidTcpStatsd) {
  const std::string json_string = R"EOF(
  {
    "cluster_name" : "fake_cluster"
  }
  )EOF";
  const std::string name = "statsd_tcp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  Stats::SinkPtr sink = factory->createStatsSink(*json_config, server, server.clusterManager());
  EXPECT_NE(sink, nullptr);
}

TEST(StatsConfigTest, InvalidTcpStatsd) {
  const std::string json_string = R"EOF(
  {}
  )EOF";
  const std::string name = "statsd_tcp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  EXPECT_THROW(factory->createStatsSink(*json_config, server, server.clusterManager()),
               EnvoyException);
}

TEST(StatsConfigTest, ValidUdpPortStatsd) {
  const std::string json_string = R"EOF(
  {
    "local_port" : 8125
  }
  )EOF";
  const std::string name = "statsd_udp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  Stats::SinkPtr sink = factory->createStatsSink(*json_config, server, server.clusterManager());
  EXPECT_NE(sink, nullptr);
}

TEST(StatsConfigTest, ValidUdpIpStatsd) {
  const std::string json_string = R"EOF(
  {
    "ip_address" : "127.0.0.1:8125"
  }
  )EOF";
  const std::string name = "statsd_udp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  Stats::SinkPtr sink = factory->createStatsSink(*json_config, server, server.clusterManager());
  EXPECT_NE(sink, nullptr);
}

TEST(StatsConfigTest, BadUdpEmptyConfig) {
  const std::string json_string = R"EOF(
  {}
  )EOF";
  const std::string name = "statsd_udp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  EXPECT_THROW(factory->createStatsSink(*json_config, server, server.clusterManager()),
               EnvoyException);
}

TEST(StatsConfigTest, BadUdpIpAndPortConfig) {
  const std::string json_string = R"EOF(
  {
    "ip_address" : "127.0.0.1:8125",
    "local_port" : 8125
  }
  )EOF";
  const std::string name = "statsd_udp";

  Json::ObjectSharedPtr json_config = Json::Factory::loadFromString(json_string);
  NiceMock<MockInstance> server;
  StatsSinkFactory* factory = Registry::FactoryRegistry<StatsSinkFactory>::getFactory(name);
  ASSERT_NE(factory, nullptr);
  EXPECT_THROW(factory->createStatsSink(*json_config, server, server.clusterManager()),
               EnvoyException);
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
