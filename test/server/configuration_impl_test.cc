#include <chrono>
#include <list>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.validate.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/api/api_impl.h"
#include "source/common/config/well_known_names.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/cluster_manager_impl.h"
#include "source/extensions/stat_sinks/statsd/config.h"
#include "source/server/configuration_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/type/v3/typed_struct.pb.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Server {
namespace Configuration {
namespace {

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
      : api_(Api::createApiForTest()),
        cluster_manager_factory_(
            server_context_, server_.admin(), server_.runtime(), server_.stats(),
            server_.threadLocal(),
            [this]() -> Network::DnsResolverSharedPtr { return this->server_.dnsResolver(); },
            server_.sslContextManager(), server_.dispatcher(), server_.localInfo(),
            server_.secretManager(), server_.messageValidationContext(), *api_,
            server_.httpContext(), server_.grpcContext(), server_.routerContext(),
            server_.accessLogManager(), server_.singletonManager(), server_.options(),
            server_.quic_stat_names_, server_) {}

  void addStatsdFakeClusterConfig(envoy::config::metrics::v3::StatsSink& sink) {
    envoy::config::metrics::v3::StatsdSink statsd_sink;
    statsd_sink.set_tcp_cluster_name("fake_cluster");
    sink.mutable_typed_config()->PackFrom(statsd_sink);
  }

  Api::ApiPtr api_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  NiceMock<Server::MockInstance> server_;
  Upstream::ProdClusterManagerFactory cluster_manager_factory_;
};

TEST_F(ConfigurationImplTest, DefaultStatsFlushInterval) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(std::chrono::milliseconds(5000), config.statsConfig().flushInterval());
  EXPECT_FALSE(config.statsConfig().flushOnAdmin());
}

TEST_F(ConfigurationImplTest, CustomStatsFlushInterval) {
  std::string json = R"EOF(
  {
    "stats_flush_interval": "0.500s",
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(std::chrono::milliseconds(500), config.statsConfig().flushInterval());
  EXPECT_FALSE(config.statsConfig().flushOnAdmin());
}

TEST_F(ConfigurationImplTest, StatsOnAdmin) {
  std::string json = R"EOF(
  {
    "stats_flush_on_admin": true,

    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_TRUE(config.statsConfig().flushOnAdmin());
}

TEST_F(ConfigurationImplTest, NegativeStatsOnAdmin) {
  std::string json = R"EOF(
  {
    "stats_flush_on_admin": false,

    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  EXPECT_THROW(TestUtility::validate(bootstrap), Envoy::ProtoValidationException);
}

// This should throw a proto validation exception in the v4 api with the oneof promotion.
TEST_F(ConfigurationImplTest, IntervalAndAdminFlush) {
  std::string json = R"EOF(
  {
    "stats_flush_on_admin": true,
    "stats_flush_interval": "0.500s",

    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(
      config.initialize(bootstrap, server_, cluster_manager_factory_), EnvoyException,
      "Only one of stats_flush_interval or stats_flush_on_admin should be set!");
}

TEST_F(ConfigurationImplTest, SetUpstreamClusterPerConnectionBufferLimit) {
  const std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [],
      "clusters": [
        {
          "name": "test_cluster",
          "type": "static",
          "connect_timeout": "0.01s",
          "per_connection_buffer_limit_bytes": 8192,
          "lb_policy": "round_robin",
          "load_assignment": {
    "endpoints": [
      {
        "lb_endpoints": [
          {
            "endpoint": {
              "address": {
                "socket_address": {
                  "address": "127.0.0.1",
                  "port_value": 9999
                }
              }
            }
          }
        ]
      }
    ]
  }
        }
      ]
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  ASSERT_EQ(1U, config.clusterManager()->clusters().active_clusters_.count("test_cluster"));
  EXPECT_EQ(8192U, config.clusterManager()
                       ->clusters()
                       .active_clusters_.find("test_cluster")
                       ->second.get()
                       .info()
                       ->perConnectionBufferLimitBytes());
  server_.thread_local_.shutdownThread();
}

TEST_F(ConfigurationImplTest, NullTracerSetWhenTracingConfigurationAbsent) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [
        {
          "address": {
            "socket_address": {
              "address": "127.0.0.1",
              "port_value": 1234
            }
          },
          "filter_chains": []
        }
      ],
      "clusters": []
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  server_.local_info_.node_.set_cluster("");
  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_THAT(envoy::config::trace::v3::Tracing{},
              ProtoEq(server_.httpContext().defaultTracingConfig()));
}

TEST_F(ConfigurationImplTest, NullTracerSetWhenHttpKeyAbsentFromTracerConfiguration) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [
        {
          "address": {
            "socket_address": {
              "address": "127.0.0.1",
              "port_value": 1234
            }
          },
          "filter_chains": []
        }
      ],
      "clusters": []
    },
    "tracing": {},
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  server_.local_info_.node_.set_cluster("");
  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_THAT(envoy::config::trace::v3::Tracing{},
              ProtoEq(server_.httpContext().defaultTracingConfig()));
}

TEST_F(ConfigurationImplTest, ConfigurationFailsWhenInvalidTracerSpecified) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [
        {
          "address": {
            "socket_address": {
              "address": "127.0.0.1",
              "port_value": 1234
            }
          },
          "filter_chains": []
        }
      ],
      "clusters": []
    },
    "tracing": {
      "http": {
        "name": "invalid",
        "typed_config": {
          "@type": "type.googleapis.com/xds.type.v3.TypedStruct",
          "type_url": "type.googleapis.com/envoy.config.trace.v2.BlackHoleConfig",
          "value": {
            "collector_cluster": "cluster_0",
            "access_token_file": "/etc/envoy/envoy.cfg"
          }
        }
      }
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Didn't find a registered implementation for 'invalid' with type URL: "
                            "'envoy.config.trace.v2.BlackHoleConfig'");
}

TEST_F(ConfigurationImplTest, ProtoSpecifiedStatsSink) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners": [],
      "clusters": []
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  auto& sink = *bootstrap.mutable_stats_sinks()->Add();
  sink.set_name(Extensions::StatSinks::Statsd::StatsdName);
  addStatsdFakeClusterConfig(sink);
  server_.server_factory_context_->cluster_manager_.initializeClusters({"fake_cluster"}, {});

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(1, config.statsConfig().sinks().size());
}

TEST_F(ConfigurationImplTest, StatsSinkWithInvalidName) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners": [],
      "clusters": []
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  envoy::config::metrics::v3::StatsSink& sink = *bootstrap.mutable_stats_sinks()->Add();
  sink.set_name("envoy.invalid");

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(
      config.initialize(bootstrap, server_, cluster_manager_factory_), EnvoyException,
      "Didn't find a registered implementation for 'envoy.invalid' with type URL: ''");
}

TEST_F(ConfigurationImplTest, StatsSinkWithNoName) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners": [],
      "clusters": []
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  bootstrap.mutable_stats_sinks()->Add();

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Didn't find a registered implementation for '' with type URL: ''");
}

TEST_F(ConfigurationImplTest, StatsSinkWithNoType) {
  std::string json = R"EOF(
  {
    "static_resources": {
      "listeners": [],
      "clusters": []
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  auto& sink = *bootstrap.mutable_stats_sinks()->Add();
  xds::type::v3::TypedStruct typed_struct;
  auto untyped_struct = typed_struct.mutable_value();
  (*untyped_struct->mutable_fields())["foo"].set_string_value("bar");
  sink.mutable_typed_config()->PackFrom(typed_struct);

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException,
                            "Didn't find a registered implementation for '' with type URL: ''");
}

// An explicit non-empty LayeredRuntime is available to the server with no
// changes made.
TEST(InitialImplTest, LayeredRuntime) {
  const std::string yaml = R"EOF(
  layered_runtime:
    layers:
    - name: base
      static_layer:
        health_check:
          min_interval: 5
    - name: root
      disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy }
    - name: override
      disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy_override, append_service_cluster: true }
    - name: admin
      admin_layer: {}
  )EOF";
  const auto bootstrap = TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(yaml);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);
  EXPECT_THAT(config.runtime(), ProtoEq(bootstrap.layered_runtime()));
}

// An empty LayeredRuntime has an admin layer injected.
TEST(InitialImplTest, EmptyLayeredRuntime) {
  const std::string bootstrap_yaml = R"EOF(
  layered_runtime: {}
  )EOF";
  const auto bootstrap =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::Bootstrap>(bootstrap_yaml);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);

  const std::string expected_yaml = R"EOF(
  layers:
  - admin_layer: {}
  )EOF";
  const auto expected_runtime =
      TestUtility::parseYaml<envoy::config::bootstrap::v3::LayeredRuntime>(expected_yaml);
  EXPECT_THAT(config.runtime(), ProtoEq(expected_runtime));
}

TEST_F(ConfigurationImplTest, AdminSocketOptions) {
  std::string json = R"EOF(
  {
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      },
      "socket_options": [
         {
           "level": 1,
           "name": 2,
           "int_value": 3,
           "state": "STATE_PREBIND"
         },
         {
           "level": 4,
           "name": 5,
           "int_value": 6,
           "state": "STATE_BOUND"
         },
      ]
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);
  Network::MockListenSocket socket_mock;

  ASSERT_EQ(config.admin().socketOptions()->size(), 2);
  auto detail = config.admin().socketOptions()->at(0)->getOptionDetails(
      socket_mock, envoy::config::core::v3::SocketOption::STATE_PREBIND);
  ASSERT_NE(detail, absl::nullopt);
  EXPECT_EQ(detail->name_, Envoy::Network::SocketOptionName(1, 2, "1/2"));
  detail = config.admin().socketOptions()->at(1)->getOptionDetails(
      socket_mock, envoy::config::core::v3::SocketOption::STATE_BOUND);
  ASSERT_NE(detail, absl::nullopt);
  EXPECT_EQ(detail->name_, Envoy::Network::SocketOptionName(4, 5, "4/5"));
}

TEST_F(ConfigurationImplTest, FileAccessLogOutput) {
  std::string json = R"EOF(
  {
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);
  Network::MockListenSocket socket_mock;

  ASSERT_EQ(config.admin().accessLogs().size(), 1);
}

TEST_F(ConfigurationImplTest, ExceedLoadBalancerHostWeightsLimit) {
  const std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [],
      "clusters": [
        {
          "name": "test_cluster",
          "type": "static",
          "connect_timeout": "0.01s",
          "per_connection_buffer_limit_bytes": 8192,
          "lb_policy": "RING_HASH",
          "load_assignment": {
            "cluster_name": "load_test_cluster",
            "endpoints": [
              {
                "priority": 93
              },
              {
                "locality": {
                  "zone": "zone1"
                },
                "lb_endpoints": [
                  {
                    "endpoint": {
                      "address": {
                        "pipe": {
                          "path": "path/to/pipe"
                        }
                      }
                    },
                    "health_status": "TIMEOUT",
                    "load_balancing_weight": {
                      "value": 4294967295
                    }
                  },
                  {
                    "endpoint": {
                      "address": {
                        "pipe": {
                          "path": "path/to/pipe2"
                        }
                      }
                    },
                    "health_status": "TIMEOUT",
                    "load_balancing_weight": {
                      "value": 1
                    }
                  }
                ],
                "load_balancing_weight": {
                  "value": 122
                }
              }
            ]
          }
        }
      ]
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(
      config.initialize(bootstrap, server_, cluster_manager_factory_), EnvoyException,
      "The sum of weights of all upstream hosts in a locality exceeds 4294967295");
}

TEST_F(ConfigurationImplTest, ExceedLoadBalancerLocalityWeightsLimit) {
  const std::string json = R"EOF(
  {
    "static_resources": {
      "listeners" : [],
      "clusters": [
        {
          "name": "test_cluster",
          "type": "static",
          "connect_timeout": "0.01s",
          "per_connection_buffer_limit_bytes": 8192,
          "lb_policy": "RING_HASH",
          "load_assignment": {
            "cluster_name": "load_test_cluster",
            "endpoints": [
              {
                "priority": 93
              },
              {
                "locality": {
                  "zone": "zone1"
                },
                "lb_endpoints": [
                  {
                    "endpoint": {
                      "address": {
                        "pipe": {
                          "path": "path/to/pipe"
                        }
                      }
                    },
                    "health_status": "TIMEOUT",
                    "load_balancing_weight": {
                      "value": 7
                    }
                  }
                ],
                "load_balancing_weight": {
                  "value": 4294967295
                }
              },
              {
                "locality": {
                  "region": "domains",
                  "sub_zone": "sub_zone1"
                },
                "lb_endpoints": [
                  {
                    "endpoint": {
                      "address": {
                        "pipe": {
                          "path": "path/to/pipe"
                        }
                      }
                    },
                    "health_status": "TIMEOUT",
                    "load_balancing_weight": {
                      "value": 8
                    }
                  }
                ],
                "load_balancing_weight": {
                  "value": 2
                }
              }
            ]
          },
          "lb_subset_config": {
            "fallback_policy": "ANY_ENDPOINT",
            "subset_selectors": {
              "keys": [
                "x"
              ]
            },
            "locality_weight_aware": "true"
          },
          "common_lb_config": {
            "healthy_panic_threshold": {
              "value": 0.8
            },
            "locality_weighted_lb_config": {
            }
          }
        }
      ]
    },
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);

  MainImpl config;
  EXPECT_THROW_WITH_MESSAGE(
      config.initialize(bootstrap, server_, cluster_manager_factory_), EnvoyException,
      "The sum of weights of all localities at the same priority exceeds 4294967295");
}

TEST_F(ConfigurationImplTest, KillTimeoutWithoutSkew) {
  const std::string json = R"EOF(
  {
    "watchdog": {
      "kill_timeout": "1.0s",
    },
  })EOF";

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json, bootstrap);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(config.workerWatchdogConfig().killTimeout(), std::chrono::milliseconds(1000));
  EXPECT_EQ(config.mainThreadWatchdogConfig().killTimeout(), std::chrono::milliseconds(1000));
}

TEST_F(ConfigurationImplTest, CanSkewsKillTimeout) {
  const std::string json = R"EOF(
  {
    "watchdog": {
      "kill_timeout": "1.0s",
      "max_kill_timeout_jitter": "0.5s"
    },
  })EOF";

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json, bootstrap);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_LT(std::chrono::milliseconds(1000), config.mainThreadWatchdogConfig().killTimeout());
  EXPECT_LT(std::chrono::milliseconds(1000), config.workerWatchdogConfig().killTimeout());
  EXPECT_GE(std::chrono::milliseconds(1500), config.mainThreadWatchdogConfig().killTimeout());
  EXPECT_GE(std::chrono::milliseconds(1500), config.workerWatchdogConfig().killTimeout());
}

TEST_F(ConfigurationImplTest, DoesNotSkewIfKillTimeoutDisabled) {
  const std::string json = R"EOF(
  {
    "watchdog": {
      "max_kill_timeout_jitter": "0.5s"
    },
  })EOF";

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json, bootstrap);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(config.mainThreadWatchdogConfig().killTimeout(), std::chrono::milliseconds(0));
  EXPECT_EQ(config.workerWatchdogConfig().killTimeout(), std::chrono::milliseconds(0));
}

TEST_F(ConfigurationImplTest, ShouldErrorIfBothWatchdogsAndWatchdogSet) {
  const std::string json = R"EOF( { "watchdogs": {}, "watchdog": {}})EOF";

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json, bootstrap);

  MainImpl config;

  EXPECT_THROW_WITH_MESSAGE(config.initialize(bootstrap, server_, cluster_manager_factory_),
                            EnvoyException, "Only one of watchdog or watchdogs should be set!");
}

TEST_F(ConfigurationImplTest, CanSetMultiWatchdogConfigs) {
  const std::string json = R"EOF( { "watchdogs": {
    "main_thread_watchdog" : {
      miss_timeout : "2s"
    },
    "worker_watchdog" : {
      miss_timeout : "0.5s"
    }
  }})EOF";

  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromJson(json, bootstrap);

  MainImpl config;
  config.initialize(bootstrap, server_, cluster_manager_factory_);

  EXPECT_EQ(config.mainThreadWatchdogConfig().missTimeout(), std::chrono::milliseconds(2000));
  EXPECT_EQ(config.workerWatchdogConfig().missTimeout(), std::chrono::milliseconds(500));
}

TEST_F(ConfigurationImplTest, DEPRECATED_FEATURE_TEST(DeprecatedAccessLogPathWithAccessLog)) {
  std::string json = R"EOF(
  {
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          }
        }
      ],
      access_log_path: "/dev/null",
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);
  Network::MockListenSocket socket_mock;

  ASSERT_EQ(config.admin().accessLogs().size(), 2);
}

TEST_F(ConfigurationImplTest, AccessLogWithFilter) {
  std::string json = R"EOF(
  {
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          },
          "filter": {
            "not_health_check_filter":{
            }
          }
        }
      ],
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);

  ASSERT_EQ(config.admin().accessLogs().size(), 1);
}

TEST_F(ConfigurationImplTest, DEPRECATED_FEATURE_TEST(DeprecatedAccessLogPathWithFilter)) {
  std::string json = R"EOF(
  {
    "admin": {
      "access_log": [
        {
          "name": "envoy.access_loggers.file",
          "typed_config": {
            "@type": "type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog",
            "path": "/dev/null"
          },
          "filter": {
            "not_health_check_filter":{
            }
          }
        }
      ],
      access_log_path: "/dev/null",
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);

  ASSERT_EQ(config.admin().accessLogs().size(), 2);
}

TEST_F(ConfigurationImplTest, EmptyAdmin) {
  std::string json = R"EOF(
  {
    "admin": {}
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);

  ASSERT_EQ(config.admin().accessLogs().size(), 0);
}

TEST_F(ConfigurationImplTest, DEPRECATED_FEATURE_TEST(DeprecatedAccessLogPath)) {
  std::string json = R"EOF(
  {
    "admin": {
      access_log_path: "/dev/null",
      "address": {
        "socket_address": {
          "address": "1.2.3.4",
          "port_value": 5678
        }
      }
    }
  }
  )EOF";

  auto bootstrap = Upstream::parseBootstrapFromV3Json(json);
  NiceMock<Server::MockInstance> server;
  InitialImpl config(bootstrap);
  config.initAdminAccessLog(bootstrap, server_);
  Network::MockListenSocket socket_mock;

  ASSERT_EQ(config.admin().accessLogs().size(), 1);
}
} // namespace
} // namespace Configuration
} // namespace Server
} // namespace Envoy
