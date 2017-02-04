#include "server/configuration_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"

using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Server {
namespace Configuration {

TEST(FilterChainUtility, buildFilterChain) {
  Network::MockConnection connection;
  std::list<NetworkFilterFactoryCb> factories;
  ReadyWatcher watcher;
  NetworkFilterFactoryCb factory = [&](Network::FilterManager&) -> void { watcher.ready(); };
  factories.push_back(factory);
  factories.push_back(factory);

  EXPECT_CALL(watcher, ready()).Times(2);
  EXPECT_CALL(connection, initializeReadFilters()).WillOnce(Return(true));
  EXPECT_EQ(FilterChainUtility::buildFilterChain(connection, factories), true);
}

TEST(FilterChainUtility, buildFilterChainFailWithBadFilters) {
  Network::MockConnection connection;
  std::list<NetworkFilterFactoryCb> factories;
  EXPECT_CALL(connection, initializeReadFilters()).WillOnce(Return(false));
  EXPECT_EQ(FilterChainUtility::buildFilterChain(connection, factories), false);
}

TEST(ConfigurationImplTest, DefaultStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  config.initialize(*loader);

  EXPECT_EQ(std::chrono::milliseconds(5000), config.statsFlushInterval());
}

TEST(ConfigurationImplTest, CustomStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "stats_flush_interval_ms": 500,

    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  config.initialize(*loader);

  EXPECT_EQ(std::chrono::milliseconds(500), config.statsFlushInterval());
}

TEST(ConfigurationImplTest, EmptyFilter) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  config.initialize(*loader);

  EXPECT_EQ(1U, config.listeners().size());
}

TEST(ConfigurationImplTest, BadListenerConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters": [],
        "test": "a"
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  EXPECT_THROW(config.initialize(*loader), Json::Exception);
}

TEST(ConfigurationImplTest, NotSetServiceClusterWhenLSTracing) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    },
    "tracing": {
      "http": {
        "driver": {
          "type": "lightstep",
          "access_token_file": "/etc/envoy/envoy.cfg"
        }
      }
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  std::string cluster_not_set = "";
  EXPECT_CALL(server.local_info_, clusterName()).WillOnce(ReturnRef(cluster_not_set));

  MainImpl config(server);
  EXPECT_THROW(config.initialize(*loader), EnvoyException);
}

} // Configuration
} // Server
