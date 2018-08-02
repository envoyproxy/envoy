#include <chrono>
#include <list>
#include <string>

#include "common/config/bootstrap_json.h"
#include "common/config/well_known_names.h"
#include "common/json/json_loader.h"
#include "common/upstream/cluster_manager_impl.h"

#include "server/configuration_impl.h"

#include "extensions/stat_sinks/well_known_names.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace Configuration {

TEST(FilterChainUtility, buildFilterChain) {
  Network::MockConnection connection;
  std::vector<Network::FilterFactoryCb> factories;
  ReadyWatcher watcher;
  Network::FilterFactoryCb factory = [&](Network::FilterManager&) -> void { watcher.ready(); };
  factories.push_back(factory);
  factories.push_back(factory);

  EXPECT_CALL(watcher, ready()).Times(2);
  EXPECT_CALL(connection, initializeReadFilters()).WillOnce(Return(true));
  EXPECT_EQ(FilterChainUtility::buildFilterChain(connection, factories), true);
}

TEST(FilterChainUtility, buildFilterChainFailWithBadFilters) {
  Network::MockConnection connection;
  std::vector<Network::FilterFactoryCb> factories;
  EXPECT_CALL(connection, initializeReadFilters()).WillOnce(Return(false));
  EXPECT_EQ(FilterChainUtility::buildFilterChain(connection, factories), false);
}

class ConfigurationImplTest : public testing::Test {
protected:
  ConfigurationImplTest()
      : cluster_manager_factory_(server_.runtime(), server_.stats(), server_.threadLocal(),
                                 server_.random(), server_.dnsResolver(),
                                 server_.sslContextManager(), server_.dispatcher(),
                                 server_.localInfo(), server_.secretManager()) {}

  NiceMock<Server::MockInstance> server_;
  Upstream::ProdClusterManagerFactory cluster_manager_factory_;
};

TEST_F(ConfigurationImplTest, DefaultStatsFlushInterval) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(std::chrono::milliseconds(5000), config.statsFlushInterval());
}

TEST_F(ConfigurationImplTest, CustomStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "stats_flush_interval_ms": 500,

    "cluster_manager": {
      "clusters": []
    },

    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(std::chrono::milliseconds(500), config.statsFlushInterval());
}

TEST_F(ConfigurationImplTest, SetUpstreamClusterPerConnectionBufferLimit) {
  const std::string json = R"EOF(
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
    },
    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  ASSERT_EQ(1U, config.clusterManager()->clusters().count("test_cluster"));
  EXPECT_EQ(8192U, config.clusterManager()
                       ->clusters()
                       .find("test_cluster")
                       ->second.get()
                       .info()
                       ->perConnectionBufferLimitBytes());
  server_.thread_local_.shutdownThread();
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
    },
    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  server_.local_info_.node_.set_cluster("");
  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

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
            "collector_cluster": "cluster_0",
            "access_token_file": "/etc/envoy/envoy.cfg"
          }
        }
      }
    },
    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  server_.local_info_.node_.set_cluster("");
  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_NE(nullptr, dynamic_cast<Tracing::HttpNullTracer*>(&config.httpTracer()));
}

TEST_F(ConfigurationImplTest, ConfigurationFailsWhenInvalidTracerSpecified) {
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
            "collector_cluster": "cluster_0",
            "access_token_file": "/etc/envoy/envoy.cfg"
          }
        }
      }
    },
    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);
  bootstrap.mutable_tracing()->mutable_http()->set_name("invalid");
  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'invalid'");
}

TEST_F(ConfigurationImplTest, ProtoSpecifiedStatsSink) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "cluster_manager": {
      "clusters": []
    },

    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  auto& sink = *bootstrap.mutable_stats_sinks()->Add();
  sink.set_name(Extensions::StatSinks::StatsSinkNames::get().Statsd);
  auto& field_map = *sink.mutable_config()->mutable_fields();
  field_map["tcp_cluster_name"].set_string_value("fake_cluster");

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(1, config.statsSinks().size());
}

TEST_F(ConfigurationImplTest, StatsSinkWithInvalidName) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "cluster_manager": {
      "clusters": []
    },

    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  envoy::config::metrics::v2::StatsSink& sink = *bootstrap.mutable_stats_sinks()->Add();
  sink.set_name("envoy.invalid");
  auto& field_map = *sink.mutable_config()->mutable_fields();
  field_map["tcp_cluster_name"].set_string_value("fake_cluster");

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Didn't find a registered implementation for name: 'envoy.invalid'");
}

TEST_F(ConfigurationImplTest, StatsSinkWithNoName) {
  std::string json = R"EOF(
  {
    "listeners": [],

    "cluster_manager": {
      "clusters": []
    },

    "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
  }
  )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  auto& sink = *bootstrap.mutable_stats_sinks()->Add();
  auto& field_map = *sink.mutable_config()->mutable_fields();
  field_map["tcp_cluster_name"].set_string_value("fake_cluster");

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Provided name for static registration lookup was empty.");
}

TEST_F(ConfigurationImplTest, StaticSecretRead) {
  std::string json =
      R"EOF(
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
      "admin": {"access_log_path": "/dev/null", "address": "tcp://1.2.3.4:5678"}
    }
    )EOF";

  envoy::config::bootstrap::v2::Bootstrap bootstrap = TestUtility::parseBootstrapFromJson(json);

  auto secret_config = bootstrap.mutable_static_resources()->mutable_secrets()->Add();

  std::string yaml =
      R"EOF(
  name: "abc.com"
  tls_certificate:
    certificate_chain:
      filename: "{{ test_rundir }}/test/config/integration/certs/cacert.pem"
    private_key:
      filename: "{{ test_rundir }}/test/config/integration/certs/cakey.pem"
  )EOF";

  MessageUtil::loadFromYaml(TestEnvironment::substitute(yaml), *secret_config);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  auto secret = server_.secretManager().findStaticTlsCertificate("abc.com");

  ASSERT_NE(secret, nullptr);

  const std::string cert_pem = "{{ test_rundir }}/test/config/integration/certs/cacert.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(cert_pem)),
            secret->certificateChain());

  const std::string key_pem = "{{ test_rundir }}/test/config/integration/certs/cakey.pem";
  EXPECT_EQ(TestEnvironment::readFileToStringForTest(TestEnvironment::substitute(key_pem)),
            secret->privateKey());
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
