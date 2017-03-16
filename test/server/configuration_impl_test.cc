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

TEST(ConfigurationImplTest, DefaultListenerPerConnectionBufferLimit) {
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

  EXPECT_EQ(1024 * 1024U, config.listeners().back()->perConnectionBufferLimitBytes());
}

TEST(ConfigurationImplTest, SetListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters": [],
        "per_connection_buffer_limit_bytes": 8192
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

  EXPECT_EQ(8192U, config.listeners().back()->perConnectionBufferLimitBytes());
}

TEST(ConfigurationImplTest, VerifySubjectAltNameConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters" : [],
        "ssl_context" : {
          "cert_chain_file" : "test/common/ssl/test_data/approved_with_uri_san.crt",
          "private_key_file" : "test/common/ssl/test_data/private_key_with_uri_san.pem",
          "verify_subject_alt_name" : [
            "localhost",
            "127.0.0.1"
          ]
        }
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

  EXPECT_TRUE(config.listeners().back()->sslContext() != nullptr);
}

TEST(ConfigurationImplTest, SetUpstreamClusterPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "listeners" : [],
    "cluster_manager": {
      "clusters": [
        {
          "name": "test_cluster",
          "type": "static",
          "connect_timeout_ms": 1,
          "per_connection_buffer_limit_bytes": 8192,
          "lb_type": "round_robin",
          "hosts": []
        }
      ]
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  config.initialize(*loader);

  ASSERT_EQ(1U, config.clusterManager().clusters().count("test_cluster"));
  EXPECT_EQ(8192U, config.clusterManager()
                       .clusters()
                       .find("test_cluster")
                       ->second.get()
                       .info()
                       ->perConnectionBufferLimitBytes());
  server.thread_local_.shutdownThread();
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

TEST(ConfigurationImplTest, BadFilterConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "port" : 1234,
        "filters": [
          {
            "type" : "type",
            "name" : "name",
            "config" : {}
          }
        ]
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

TEST(ConfigurationImplTest, ServiceClusterNotSetWhenLSTracing) {
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
  server.local_info_.cluster_name_ = "";
  MainImpl config(server);
  EXPECT_THROW(config.initialize(*loader), EnvoyException);
}

TEST(ConfigurationImplTest, UnsupportedDriverType) {
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
          "type": "unknown",
          "access_token_file": "/etc/envoy/envoy.cfg"
        }
      }
    }
  }
  )EOF";

  Json::ObjectPtr loader = Json::Factory::LoadFromString(json);

  NiceMock<Server::MockInstance> server;
  MainImpl config(server);
  EXPECT_THROW(config.initialize(*loader), EnvoyException);
}

} // Configuration
} // Server
