#include <chrono>
#include <list>
#include <string>

#include "server/configuration_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
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

class ConfigurationImplTest : public testing::Test {
protected:
  ConfigurationImplTest() : cluster_manager_factory_(
      server_.runtime(), server_.stats(), server_.threadLocal(), server_.random(), server_.dnsResolver(),
      server_.sslContextManager(), server_.dispatcher(), server_.localInfo()) {}

  NiceMock<Server::MockInstance> server_;
  Upstream::ProdClusterManagerFactory cluster_manager_factory_;
};

TEST_F(ConfigurationImplTest, DefaultStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_EQ(std::chrono::milliseconds(5000), config.statsFlushInterval());
}

TEST_F(ConfigurationImplTest, CustomStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "stats_flush_interval_ms": 500,

    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_EQ(std::chrono::milliseconds(500), config.statsFlushInterval());
}

TEST_F(ConfigurationImplTest, EmptyFilter) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_EQ(1U, config.listeners().size());
}

TEST_F(ConfigurationImplTest, DefaultListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_EQ(1024 * 1024U, config.listeners().back()->perConnectionBufferLimitBytes());
}

TEST_F(ConfigurationImplTest, SetListenerPerConnectionBufferLimit) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": [],
        "per_connection_buffer_limit_bytes": 8192
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_EQ(8192U, config.listeners().back()->perConnectionBufferLimitBytes());
}

TEST_F(ConfigurationImplTest, VerifySubjectAltNameConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters" : [],
        "ssl_context" : {
          "cert_chain_file" : "{{ test_rundir }}/test/common/ssl/test_data/san_uri_cert.pem",
          "private_key_file" : "{{ test_rundir }}/test/common/ssl/test_data/san_uri_key.pem",
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

  Json::ObjectSharedPtr loader = TestEnvironment::jsonLoadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_TRUE(config.listeners().back()->sslContext() != nullptr);
}

TEST_F(ConfigurationImplTest, SetUpstreamClusterPerConnectionBufferLimit) {
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
          "hosts": [
            { "url" : "tcp://127.0.0.1:9999" }
          ]
        }
      ]
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  ASSERT_EQ(1U, config.clusterManager().clusters().count("test_cluster"));
  EXPECT_EQ(8192U, config.clusterManager()
                       .clusters()
                       .find("test_cluster")
                       ->second.get()
                       .info()
                       ->perConnectionBufferLimitBytes());
  server_.thread_local_.shutdownThread();
}

TEST_F(ConfigurationImplTest, BadListenerConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": [],
        "test": "a"
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  EXPECT_THROW(config.initialize(*loader), Json::Exception);
}

TEST_F(ConfigurationImplTest, BadFilterConfig) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
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

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  MainImpl config(server_, cluster_manager_factory_);
  EXPECT_THROW(config.initialize(*loader), Json::Exception);
}

TEST_F(ConfigurationImplTest, ServiceClusterNotSetWhenLSTracing) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
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
          "config": {
            "access_token_file": "/etc/envoy/envoy.cfg"
          }
        }
      }
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  server_.local_info_.cluster_name_ = "";
  MainImpl config(server_, cluster_manager_factory_);
  EXPECT_THROW(config.initialize(*loader), EnvoyException);
}

TEST_F(ConfigurationImplTest, NullTracerSetWhenTracingConfigurationAbsent) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  server_.local_info_.cluster_name_ = "";
  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_NE(nullptr, dynamic_cast<Tracing::HttpNullTracer*>(&config.httpTracer()));
}

TEST_F(ConfigurationImplTest, NullTracerSetWhenHttpKeyAbsentFromTracerConfiguration) {
  std::string json = R"EOF(
  {
    "listeners" : [
      {
        "address": "tcp://127.0.0.1:1234",
        "filters": []
      }
    ],
    "cluster_manager": {
      "clusters": []
    },
    "tracing": {
      "not_http": {
        "driver": {
          "type": "lightstep",
          "config": {
            "access_token_file": "/etc/envoy/envoy.cfg"
          }
        }
      }
    }
  }
  )EOF";

  Json::ObjectSharedPtr loader = Json::Factory::loadFromString(json);

  server_.local_info_.cluster_name_ = "";
  MainImpl config(server_, cluster_manager_factory_);
  config.initialize(*loader);

  EXPECT_NE(nullptr, dynamic_cast<Tracing::HttpNullTracer*>(&config.httpTracer()));
}

} // Configuration
} // Server
} // Envoy
