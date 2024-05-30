#include "test/common/integration/test_server.h"

#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/common/listener_manager/listener_manager_impl.h"
#include "source/common/quic/quic_server_transport_socket_factory.h"
#include "source/common/quic/server_codec_impl.h"
#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/tls/context_config_impl.h"
#include "source/common/tls/server_context_impl.h"
#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator_config.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/udp_packet_writer/default/config.h"
#include "source/server/hot_restart_nop_impl.h"
#include "source/server/instance_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "extension_registry.h"

namespace Envoy {
namespace {

inline constexpr absl::string_view HTTP_PROXY_CONFIG = R"EOF(
static_resources {
  listeners {
    name: "listener_proxy"
    address {
      socket_address {
        address: "127.0.0.1"
        port_value: 0
      }
    }
    filter_chains {
      filters {
        name: "envoy.filters.network.http_connection_manager"
        typed_config {
          [type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager] {
            stat_prefix: "remote_hcm"
            route_config {
              name: "remote_route"
              virtual_hosts {
                name: "remote_service"
                domains: "*"
                routes {
                  match {
                    prefix: "/"
                  }
                  route {
                    cluster: "cluster_proxy"
                  }
                }
              }
              response_headers_to_add {
                header {
                  key: "x-proxy-response"
                  value: "true"
                }
                append_action: OVERWRITE_IF_EXISTS_OR_ADD
              }
            }
            http_filters {
              name: "envoy.filters.http.local_error"
              typed_config {
                [type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError] {
                }
              }
            }
            http_filters {
              name: "envoy.filters.http.dynamic_forward_proxy"
              typed_config {
                [type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig] {
                  dns_cache_config {
                    name: "base_dns_cache"
                    dns_lookup_family: ALL
                    dns_refresh_rate {
                      seconds: 60
                    }
                    host_ttl {
                      seconds: 86400
                    }
                    dns_failure_refresh_rate {
                      base_interval {
                        seconds: 2
                      }
                      max_interval {
                        seconds: 10
                      }
                    }
                    dns_query_timeout {
                      seconds: 25
                    }
                    typed_dns_resolver_config {
                      name: "envoy.network.dns_resolver.getaddrinfo"
                      typed_config {
                        [type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig] {
                        }
                      }
                    }
                    dns_min_refresh_rate {
                      seconds: 20
                    }
                  }
                }
              }
            }
            http_filters {
              name: "envoy.router"
              typed_config {
                [type.googleapis.com/envoy.extensions.filters.http.router.v3.Router] {
                }
              }
            }
          }
        }
      }
    }
  }
  clusters {
    name: "cluster_proxy"
    connect_timeout {
      seconds: 30
    }
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type {
      name: "envoy.clusters.dynamic_forward_proxy"
      typed_config {
        [type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig] {
          dns_cache_config {
            name: "base_dns_cache"
            dns_lookup_family: ALL
            dns_refresh_rate {
              seconds: 60
            }
            host_ttl {
              seconds: 86400
            }
            dns_failure_refresh_rate {
              base_interval {
                seconds: 2
              }
              max_interval {
                seconds: 10
              }
            }
            dns_query_timeout {
              seconds: 25
            }
            typed_dns_resolver_config {
              name: "envoy.network.dns_resolver.getaddrinfo"
              typed_config {
                [type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig] {
                }
              }
            }
            dns_min_refresh_rate {
              seconds: 20
            }
          }
        }
      }
    }
  }
}
layered_runtime {
  layers {
    name: "static_layer_0"
    static_layer {
      fields {
        key: "envoy"
        value {
          struct_value {
            fields {
              key: "disallow_global_stats"
              value {
                bool_value: true
              }
            }
          }
        }
      }
    }
  }
}
)EOF";

inline constexpr absl::string_view HTTPS_PROXY_CONFIG = R"EOF(
static_resources {
  listeners {
    name: "listener_proxy"
    address {
      socket_address {
        address: "127.0.0.1"
        port_value: 0
      }
    }
    filter_chains {
      filters {
        name: "envoy.filters.network.http_connection_manager"
        typed_config {
          [type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager] {
            stat_prefix: "remote_hcm"
            route_config {
              name: "remote_route"
              virtual_hosts {
                name: "remote_service"
                domains: "*"
                routes {
                  match {
                    connect_matcher {
                    }
                  }
                  route {
                    cluster: "cluster_proxy"
                    upgrade_configs {
                      upgrade_type: "CONNECT"
                    }
                  }
                }
              }
              response_headers_to_add {
                header {
                  key: "x-response-header-that-should-be-stripped"
                  value: "true"
                }
                append_action: OVERWRITE_IF_EXISTS_OR_ADD
              }
            }
            http_filters {
              name: "envoy.filters.http.local_error"
              typed_config {
                [type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError] {
                }
              }
            }
            http_filters {
              name: "envoy.filters.http.dynamic_forward_proxy"
              typed_config {
                [type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig] {
                  dns_cache_config {
                    name: "base_dns_cache"
                    dns_lookup_family: ALL
                    dns_refresh_rate {
                      seconds: 60
                    }
                    host_ttl {
                      seconds: 86400
                    }
                    dns_failure_refresh_rate {
                      base_interval {
                        seconds: 2
                      }
                      max_interval {
                        seconds: 10
                      }
                    }
                    dns_query_timeout {
                      seconds: 25
                    }
                    typed_dns_resolver_config {
                      name: "envoy.network.dns_resolver.getaddrinfo"
                      typed_config {
                        [type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig] {
                        }
                      }
                    }
                    dns_min_refresh_rate {
                      seconds: 20
                    }
                  }
                }
              }
            }
            http_filters {
              name: "envoy.router"
              typed_config {
                [type.googleapis.com/envoy.extensions.filters.http.router.v3.Router] {
                }
              }
            }
          }
        }
      }
    }
  }
  clusters {
    name: "cluster_proxy"
    connect_timeout {
      seconds: 30
    }
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type {
      name: "envoy.clusters.dynamic_forward_proxy"
      typed_config {
        [type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig] {
          dns_cache_config {
            name: "base_dns_cache"
            dns_lookup_family: ALL
            dns_refresh_rate {
              seconds: 60
            }
            host_ttl {
              seconds: 86400
            }
            dns_failure_refresh_rate {
              base_interval {
                seconds: 2
              }
              max_interval {
                seconds: 10
              }
            }
            dns_query_timeout {
              seconds: 25
            }
            typed_dns_resolver_config {
              name: "envoy.network.dns_resolver.getaddrinfo"
              typed_config {
                [type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig] {
                }
              }
            }
            dns_min_refresh_rate {
              seconds: 20
            }
          }
        }
      }
    }
  }
}
layered_runtime {
  layers {
    name: "static_layer_0"
    static_layer {
      fields {
        key: "envoy"
        value {
          struct_value {
            fields {
              key: "disallow_global_stats"
              value {
                bool_value: true
              }
            }
          }
        }
      }
    }
  }
}
)EOF";
} // namespace

TestServer::TestServer()
    : api_(Api::createApiForTest(stats_store_, time_system_)),
      version_(TestEnvironment::getIpVersionsForTest()[0]), upstream_config_(time_system_),
      port_(0) {
  std::string runfiles_error;
  runfiles_ = std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles>{
      bazel::tools::cpp::runfiles::Runfiles::CreateForTest(&runfiles_error)};
  RELEASE_ASSERT(TestEnvironment::getOptionalEnvVar("NORUNFILES").has_value() ||
                     runfiles_ != nullptr,
                 runfiles_error);
  TestEnvironment::setRunfiles(runfiles_.get());
  ON_CALL(factory_context_.server_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  ON_CALL(factory_context_, statsScope())
      .WillByDefault(testing::ReturnRef(*stats_store_.rootScope()));
  ON_CALL(factory_context_, sslContextManager())
      .WillByDefault(testing::ReturnRef(context_manager_));

  Envoy::ExtensionRegistry::registerFactories();
}

void TestServer::start(TestServerType type) {
  ASSERT(!upstream_);
  // pre-setup: see https://github.com/envoyproxy/envoy/blob/main/test/test_runner.cc
  Logger::Context logging_state(spdlog::level::level_enum::err,
                                "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v", lock_, false, false);
  // end pre-setup
  Network::DownstreamTransportSocketFactoryPtr factory;

  Extensions::TransportSockets::Tls::forceRegisterServerContextFactoryImpl();
  switch (type) {
  case TestServerType::HTTP3:
    // Make sure if extensions aren't statically linked QUIC will work.
    Quic::forceRegisterQuicServerTransportSocketConfigFactory();
    Network::forceRegisterUdpDefaultWriterFactoryFactory();
    Quic::forceRegisterQuicHttpServerConnectionFactoryImpl();
    Quic::forceRegisterEnvoyQuicCryptoServerStreamFactoryImpl();
    Quic::forceRegisterQuicServerTransportSocketConfigFactory();
    Quic::forceRegisterEnvoyQuicProofSourceFactoryImpl();
    Quic::forceRegisterEnvoyDeterministicConnectionIdGeneratorConfigFactory();

    // envoy.quic.crypto_stream.server.quiche
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP3;
    upstream_config_.udp_fake_upstream_ = FakeUpstreamConfig::UdpConfig();
    factory = createQuicUpstreamTlsContext(factory_context_);
    break;
  case TestServerType::HTTP2_WITH_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP2;
    factory = createUpstreamTlsContext(factory_context_);
    break;
  case TestServerType::HTTP1_WITHOUT_TLS:
    upstream_config_.upstream_protocol_ = Http::CodecType::HTTP1;
    factory = Network::Test::createRawBufferDownstreamSocketFactory();
    break;
  case TestServerType::HTTP_PROXY: {
    Server::forceRegisterDefaultListenerManagerFactoryImpl();
    Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
    Server::forceRegisterConnectionHandlerFactoryImpl();

    std::string config_path =
        TestEnvironment::writeStringToFileForTest("config.pb_text", std::string(HTTP_PROXY_CONFIG));
    test_server_ = IntegrationTestServer::create(config_path, Network::Address::IpVersion::v4,
                                                 nullptr, nullptr, {}, time_system_, *api_);
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Http proxy is now running");
    return;
  }
  case TestServerType::HTTPS_PROXY: {
    Server::forceRegisterDefaultListenerManagerFactoryImpl();
    Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
    Server::forceRegisterConnectionHandlerFactoryImpl();
    std::string config_path = TestEnvironment::writeStringToFileForTest(
        "config.pb_text", std::string(HTTPS_PROXY_CONFIG));
    test_server_ = IntegrationTestServer::create(config_path, Network::Address::IpVersion::v4,
                                                 nullptr, nullptr, {}, time_system_, *api_);
    test_server_->waitUntilListenersReady();
    ENVOY_LOG_MISC(debug, "Https proxy is now running");
    return;
  }
  }

  upstream_ = std::make_unique<AutonomousUpstream>(std::move(factory), port_, version_,
                                                   upstream_config_, true);

  // Legacy behavior for cronet tests.
  if (type == TestServerType::HTTP3) {
    upstream_->setResponseHeaders(
        std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
            {{":status", "200"},
             {"Cache-Control", "max-age=0"},
             {"Content-Type", "text/plain"},
             {"X-Original-Url", "https://test.example.com:6121/simple.txt"}})));
    upstream_->setResponseBody("This is a simple text file served by QUIC.\n");
  }

  ENVOY_LOG_MISC(debug, "Upstream now listening on {}", upstream_->localAddress()->asString());
}

void TestServer::shutdown() {
  ASSERT(upstream_ || test_server_);
  upstream_.reset();
  test_server_.reset();
}

std::string TestServer::getAddress() const {
  ASSERT(upstream_);
  return upstream_->localAddress()->asString();
}

std::string TestServer::getIpAddress() const {
  ASSERT(upstream_);
  return upstream_->localAddress()->ip()->addressAsString();
}

int TestServer::getPort() const {
  ASSERT(upstream_ || test_server_);
  if (upstream_) {
    return upstream_->localAddress()->ip()->port();
  }
  std::atomic<uint32_t> port = 0;
  absl::Notification port_set;
  test_server_->server().dispatcher().post([&]() {
    auto listeners = test_server_->server().listenerManager().listeners();
    auto listener_it = listeners.cbegin();
    auto socket_factory_it = listener_it->get().listenSocketFactories().begin();
    const auto listen_addr = (*socket_factory_it)->localAddress();
    port = listen_addr->ip()->port();
    port_set.Notify();
  });
  port_set.WaitForNotification();
  return port;
}

void TestServer::setHeadersAndData(absl::string_view header_key, absl::string_view header_value,
                                   absl::string_view response_body) {
  ASSERT(upstream_);
  upstream_->setResponseHeaders(
      std::make_unique<Http::TestResponseHeaderMapImpl>(Http::TestResponseHeaderMapImpl(
          {{std::string(header_key), std::string(header_value)}, {":status", "200"}})));
  upstream_->setResponseBody(std::string(response_body));
}

void TestServer::setResponse(const absl::flat_hash_map<std::string, std::string>& headers,
                             absl::string_view body,
                             const absl::flat_hash_map<std::string, std::string>& trailers) {
  ASSERT(upstream_);
  Http::TestResponseHeaderMapImpl new_headers;
  for (const auto& [key, value] : headers) {
    new_headers.addCopy(key, value);
  }
  new_headers.addCopy(":status", "200");
  upstream_->setResponseHeaders(std::make_unique<Http::TestResponseHeaderMapImpl>(new_headers));
  upstream_->setResponseBody(std::string(body));
  Http::TestResponseTrailerMapImpl new_trailers;
  for (const auto& [key, value] : trailers) {
    new_trailers.addCopy(key, value);
  }
  upstream_->setResponseTrailers(std::make_unique<Http::TestResponseTrailerMapImpl>(new_trailers));
}

Network::DownstreamTransportSocketFactoryPtr TestServer::createQuicUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  Extensions::TransportSockets::Tls::ContextManagerImpl context_manager{server_factory_context_};
  tls_context.mutable_common_tls_context()->add_alpn_protocols("h3");
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* certs =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  certs->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  certs->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport quic_config;
  quic_config.mutable_downstream_tls_context()->MergeFrom(tls_context);

  std::vector<std::string> server_names;
  auto& config_factory = Config::Utility::getAndCheckFactoryByName<
      Server::Configuration::DownstreamTransportSocketConfigFactory>(
      "envoy.transport_sockets.quic");

  return config_factory.createTransportSocketFactory(quic_config, factory_context, server_names)
      .value();
}

Network::DownstreamTransportSocketFactoryPtr TestServer::createUpstreamTlsContext(
    testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext>& factory_context) {
  envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
  envoy::extensions::transport_sockets::tls::v3::TlsCertificate* certs =
      tls_context.mutable_common_tls_context()->add_tls_certificates();
  certs->mutable_certificate_chain()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcert.pem"));
  certs->mutable_private_key()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamkey.pem"));
  auto* ctx = tls_context.mutable_common_tls_context()->mutable_validation_context();
  ctx->mutable_trusted_ca()->set_filename(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
  tls_context.mutable_common_tls_context()->add_alpn_protocols("h2");
  auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
      tls_context, factory_context);
  static auto* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
  return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
      std::move(cfg), context_manager_, *upstream_stats_store->rootScope(),
      std::vector<std::string>{});
}

} // namespace Envoy
