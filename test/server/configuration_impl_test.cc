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

namespace {

std::string kExpectedCertificateChain = R"EOF(-----BEGIN CERTIFICATE-----
MIIDXDCCAsWgAwIBAgIJAPF6WtmgmqziMA0GCSqGSIb3DQEBCwUAMHYxCzAJBgNV
BAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1TYW4gRnJhbmNp
c2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2luZWVyaW5nMRAw
DgYDVQQDDAdUZXN0IENBMB4XDTE4MDQwNjIwNTgwNVoXDTIwMDQwNTIwNTgwNVow
gaYxCzAJBgNVBAYTAlVTMRMwEQYDVQQIDApDYWxpZm9ybmlhMRYwFAYDVQQHDA1T
YW4gRnJhbmNpc2NvMQ0wCwYDVQQKDARMeWZ0MRkwFwYDVQQLDBBMeWZ0IEVuZ2lu
ZWVyaW5nMRowGAYDVQQDDBFUZXN0IEJhY2tlbmQgVGVhbTEkMCIGCSqGSIb3DQEJ
ARYVYmFja2VuZC10ZWFtQGx5ZnQuY29tMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCB
iQKBgQCqtS9bbVbo4ZpO1uSBCDortIibXKByL1fgl7s2uJc77+vzJnqC9uLFYygU
1Z198X6jaAjc/vUkLFVXZhOU8607Zex8X+CdZBjQqsN90X2Ste1wqJ7G5SAGhptd
/nOfb1IdGa6YtwPTlVitnMTfRgG4fh+3DA51UulCGTfJXCaC3wIDAQABo4HAMIG9
MAwGA1UdEwEB/wQCMAAwCwYDVR0PBAQDAgXgMB0GA1UdJQQWMBQGCCsGAQUFBwMC
BggrBgEFBQcDATBBBgNVHREEOjA4hh5zcGlmZmU6Ly9seWZ0LmNvbS9iYWNrZW5k
LXRlYW2CCGx5ZnQuY29tggx3d3cubHlmdC5jb20wHQYDVR0OBBYEFLEoDrcF8PTj
2t6gbcjoXQqBlAeeMB8GA1UdIwQYMBaAFJzPb3sKqs4x95aPTM2blA7vWB+0MA0G
CSqGSIb3DQEBCwUAA4GBAJr60+EyNfrdkzUzzFvRA/E7dntBhBIOWKDvB2p8Hcym
ILbC6sJdUotEUg2kxbweY20OjrpyT3jSe9o4E8SDkebybbxrQlXzNCq0XL42R5bI
TSufsKqBICwwJ47yp+NV7RsPhe8AO/GehXhTlJBBwHSX6gfvjapkUG43AmdbY19L
-----END CERTIFICATE-----
)EOF";

std::string kExpectedPrivateKey = R"EOF(-----BEGIN RSA PRIVATE KEY-----
MIICXAIBAAKBgQCqtS9bbVbo4ZpO1uSBCDortIibXKByL1fgl7s2uJc77+vzJnqC
9uLFYygU1Z198X6jaAjc/vUkLFVXZhOU8607Zex8X+CdZBjQqsN90X2Ste1wqJ7G
5SAGhptd/nOfb1IdGa6YtwPTlVitnMTfRgG4fh+3DA51UulCGTfJXCaC3wIDAQAB
AoGBAIDbb7n12QrFcTNd5vK3gSGIjy2nR72pmw3/uuPdhttJibPrMcM2FYumA5Vm
ghGVf2BdoYMgOW9qv6jPdqyTHAlkjKRU2rnqqUgiRWscHsTok6qubSeE/NtKYhM3
O2NH0Yv6Avq7aVMaZ9XpmXp/0ovpDggFBzfUZW4d3SNhFGeRAkEA1F7WwgAOh6m4
0OaZgOkTE89shSXRJHeXUegYtR1Ur3+U1Ib/Ana8JcvtmkT17iR0AUjKqDsF/4LE
OrV6Gv6+DQJBAM3HL88Ac6zxfCmmrEYDfmz9PhNj0NhDgKt0wq/mSOlCIl6EUdBu
1jFNQ2b3qDdUbNKRBBMvWJ7agl1Wk11j9ZsCQD5fXFO+EIZnopA4Kf1idufqk8TH
RpWfSiIUOK1439Zrchq5S0w98yRmsHIOruwyaJ+38U1XiHtyvI9BnYswJkECQG2d
wLL1W6lxziFl3vlA3TTzxgCQOG0rsDwla5xGAOr4xtQwimCM2l7S+Ke+H4ax23Jj
u5b4rq2YWr+b4c5q9CcCQH94/pVWoUVa2z/JlBq/1/MbcnucfWcpj8HKxpgoTD3b
t+uGq75okt7lfCeocT3Brt50w43WwPbmvQyeaC0qawU=
-----END RSA PRIVATE KEY-----
)EOF";

} // namespace

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
                                 server_.localInfo()) {}

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
  sink.set_name(Extensions::StatSinks::StatsSinkNames::get().STATSD);
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

  // inject static secrets to the bootstrap.static_resources
  std::string json_secrets =
      R"EOF(
  {
    "secrets": [
      {
        "name": "abc.com",
        "tls_certificate": {
          "cert_chain_file": "test/config/integration/certs/servercert.pem",
          "private_key_file": "test/config/integration/certs/serverkey.pem"
        }
      }
    ]
  }
  )EOF";

  Config::BootstrapJson::translateStaticSecretsBootstrap(
      *Json::Factory::loadFromString(json_secrets), bootstrap);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(kExpectedCertificateChain,
            server_.secretManager().staticSecret("abc.com")->certificateChain());
  EXPECT_EQ(kExpectedPrivateKey, server_.secretManager().staticSecret("abc.com")->privateKey());
}

} // namespace Configuration
} // namespace Server
} // namespace Envoy
