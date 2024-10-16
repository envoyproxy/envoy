#include "engine_builder.h"

#include <stdint.h>
#include <sys/socket.h>

#include "envoy/config/core/v3/socket_option.pb.h"
#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"

#if defined(__APPLE__)
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver.pb.h"
#else
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver.pb.h"
#endif
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "source/common/http/matching/inputs.h"
#include "envoy/config/core/v3/base.pb.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "fmt/core.h"
#include "library/common/internal_engine.h"
#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge.pb.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"
#include "library/common/extensions/filters/http/local_error/filter.pb.h"
#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/extensions/filters/http/socket_tag/filter.pb.h"
#include "library/common/extensions/key_value/platform/platform.pb.h"

#if defined(__APPLE__)
#include "library/common/network/apple_proxy_resolution.h"
#endif

namespace Envoy {
namespace Platform {

EngineBuilder::EngineBuilder() : callbacks_(std::make_unique<EngineCallbacks>()) {}

EngineBuilder& EngineBuilder::setNetworkThreadPriority(int thread_priority) {
  network_thread_priority_ = thread_priority;
  return *this;
}

#if !defined(__APPLE__)
EngineBuilder& EngineBuilder::setUseCares(bool use_cares) {
  use_cares_ = use_cares;
  return *this;
}

EngineBuilder& EngineBuilder::addCaresFallbackResolver(std::string host, int port) {
  cares_fallback_resolvers_.emplace_back(std::move(host), port);
  return *this;
}
#endif
EngineBuilder& EngineBuilder::setLogLevel(Logger::Logger::Levels log_level) {
  log_level_ = log_level;
  return *this;
}

EngineBuilder& EngineBuilder::setLogger(std::unique_ptr<EnvoyLogger> logger) {
  logger_ = std::move(logger);
  return *this;
}

EngineBuilder& EngineBuilder::setEngineCallbacks(std::unique_ptr<EngineCallbacks> callbacks) {
  callbacks_ = std::move(callbacks);
  return *this;
}

EngineBuilder& EngineBuilder::setOnEngineRunning(absl::AnyInvocable<void()> closure) {
  callbacks_->on_engine_running_ = std::move(closure);
  return *this;
}

EngineBuilder& EngineBuilder::setOnEngineExit(absl::AnyInvocable<void()> closure) {
  callbacks_->on_exit_ = std::move(closure);
  return *this;
}

EngineBuilder& EngineBuilder::setEventTracker(std::unique_ptr<EnvoyEventTracker> event_tracker) {
  event_tracker_ = std::move(event_tracker);
  return *this;
}

EngineBuilder& EngineBuilder::addConnectTimeoutSeconds(int connect_timeout_seconds) {
  connect_timeout_seconds_ = connect_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsRefreshSeconds(int dns_refresh_seconds) {
  dns_refresh_seconds_ = dns_refresh_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsMinRefreshSeconds(int dns_min_refresh_seconds) {
  dns_min_refresh_seconds_ = dns_min_refresh_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsFailureRefreshSeconds(int base, int max) {
  dns_failure_refresh_seconds_base_ = base;
  dns_failure_refresh_seconds_max_ = max;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds) {
  dns_query_timeout_seconds_ = dns_query_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::setDnsNumRetries(uint32_t dns_num_retries) {
  dns_num_retries_ = dns_num_retries;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsPreresolveHostnames(const std::vector<std::string>& hostnames) {
  // Add a default port of 443 for all hosts. We'll eventually change this API so it takes a single
  // {host, pair} and it can be called multiple times.
  dns_preresolve_hostnames_.clear();
  for (const std::string& hostname : hostnames) {
    dns_preresolve_hostnames_.push_back({hostname /* host */, 443 /* port */});
  }
  return *this;
}

EngineBuilder& EngineBuilder::addMaxConnectionsPerHost(int max_connections_per_host) {
  max_connections_per_host_ = max_connections_per_host;
  return *this;
}

EngineBuilder& EngineBuilder::addH2ConnectionKeepaliveIdleIntervalMilliseconds(
    int h2_connection_keepalive_idle_interval_milliseconds) {
  h2_connection_keepalive_idle_interval_milliseconds_ =
      h2_connection_keepalive_idle_interval_milliseconds;
  return *this;
}

EngineBuilder&
EngineBuilder::addH2ConnectionKeepaliveTimeoutSeconds(int h2_connection_keepalive_timeout_seconds) {
  h2_connection_keepalive_timeout_seconds_ = h2_connection_keepalive_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addKeyValueStore(std::string name,
                                               KeyValueStoreSharedPtr key_value_store) {
  key_value_stores_[std::move(name)] = std::move(key_value_store);
  return *this;
}

EngineBuilder& EngineBuilder::setAppVersion(std::string app_version) {
  app_version_ = std::move(app_version);
  return *this;
}

EngineBuilder& EngineBuilder::setAppId(std::string app_id) {
  app_id_ = std::move(app_id);
  return *this;
}

EngineBuilder& EngineBuilder::setDeviceOs(std::string device_os) {
  device_os_ = std::move(device_os);
  return *this;
}

EngineBuilder& EngineBuilder::setStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds) {
  stream_idle_timeout_seconds_ = stream_idle_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds) {
  per_try_idle_timeout_seconds_ = per_try_idle_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::enableGzipDecompression(bool gzip_decompression_on) {
  gzip_decompression_filter_ = gzip_decompression_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableBrotliDecompression(bool brotli_decompression_on) {
  brotli_decompression_filter_ = brotli_decompression_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableSocketTagging(bool socket_tagging_on) {
  socket_tagging_filter_ = socket_tagging_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableHttp3(bool http3_on) {
  enable_http3_ = http3_on;
  return *this;
}

EngineBuilder& EngineBuilder::setHttp3ConnectionOptions(std::string options) {
  http3_connection_options_ = std::move(options);
  return *this;
}

EngineBuilder& EngineBuilder::setHttp3ClientConnectionOptions(std::string options) {
  http3_client_connection_options_ = std::move(options);
  return *this;
}

EngineBuilder& EngineBuilder::addQuicHint(std::string host, int port) {
  quic_hints_.emplace_back(std::move(host), port);
  return *this;
}

EngineBuilder& EngineBuilder::addQuicCanonicalSuffix(std::string suffix) {
  quic_suffixes_.emplace_back(std::move(suffix));
  return *this;
}

EngineBuilder& EngineBuilder::setNumTimeoutsToTriggerPortMigration(int num_timeouts) {
  num_timeouts_to_trigger_port_migration_ = num_timeouts;
  return *this;
}

EngineBuilder& EngineBuilder::setForceAlwaysUsev6(bool value) {
  always_use_v6_ = value;
  return *this;
}

EngineBuilder& EngineBuilder::enableInterfaceBinding(bool interface_binding_on) {
  enable_interface_binding_ = interface_binding_on;
  return *this;
}

EngineBuilder& EngineBuilder::setUseGroIfAvailable(bool use_gro_if_available) {
  use_gro_if_available_ = use_gro_if_available;
  return *this;
}

EngineBuilder& EngineBuilder::setUdpSocketReceiveBufferSize(int32_t size) {
  udp_socket_receive_buffer_size_ = size;
  return *this;
}

EngineBuilder& EngineBuilder::setUdpSocketSendBufferSize(int32_t size) {
  udp_socket_send_buffer_size_ = size;
  return *this;
}

EngineBuilder& EngineBuilder::enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on) {
  enable_drain_post_dns_refresh_ = drain_post_dns_refresh_on;
  return *this;
}

EngineBuilder& EngineBuilder::enforceTrustChainVerification(bool trust_chain_verification_on) {
  enforce_trust_chain_verification_ = trust_chain_verification_on;
  return *this;
}

EngineBuilder& EngineBuilder::setUpstreamTlsSni(std::string sni) {
  upstream_tls_sni_ = std::move(sni);
  return *this;
}

EngineBuilder&
EngineBuilder::setQuicConnectionIdleTimeoutSeconds(int quic_connection_idle_timeout_seconds) {
  quic_connection_idle_timeout_seconds_ = quic_connection_idle_timeout_seconds;
  return *this;
}

EngineBuilder&
EngineBuilder::enablePlatformCertificatesValidation(bool platform_certificates_validation_on) {
  platform_certificates_validation_on_ = platform_certificates_validation_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableDnsCache(bool dns_cache_on, int save_interval_seconds) {
  dns_cache_on_ = dns_cache_on;
  dns_cache_save_interval_seconds_ = save_interval_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addStringAccessor(std::string name,
                                                StringAccessorSharedPtr accessor) {
  string_accessors_[std::move(name)] = std::move(accessor);
  return *this;
}

EngineBuilder& EngineBuilder::addNativeFilter(std::string name, std::string typed_config) {
  native_filter_chain_.emplace_back(NativeFilterConfig(std::move(name), std::move(typed_config)));
  return *this;
}

EngineBuilder& EngineBuilder::addNativeFilter(const std::string& name,
                                              const ProtobufWkt::Any& typed_config) {
  native_filter_chain_.push_back(NativeFilterConfig(name, typed_config));
  return *this;
}

std::string EngineBuilder::nativeNameToConfig(absl::string_view name) {
#ifdef ENVOY_ENABLE_FULL_PROTOS
  return absl::StrCat("[type.googleapis.com/"
                      "envoymobile.extensions.filters.http.platform_bridge.PlatformBridge] {"
                      "platform_filter_name: \"",
                      name, "\" }");
#else
  envoymobile::extensions::filters::http::platform_bridge::PlatformBridge proto_config;
  proto_config.set_platform_filter_name(name);
  std::string ret;
  proto_config.SerializeToString(&ret);
  ProtobufWkt::Any any_config;
  any_config.set_type_url(
      "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge");
  any_config.set_value(ret);
  any_config.SerializeToString(&ret);
  return ret;
#endif
}

EngineBuilder& EngineBuilder::addPlatformFilter(const std::string& name) {
  addNativeFilter("envoy.filters.http.platform_bridge", nativeNameToConfig(name));
  return *this;
}

EngineBuilder& EngineBuilder::addRuntimeGuard(std::string guard, bool value) {
  runtime_guards_.emplace_back(std::move(guard), value);
  return *this;
}

EngineBuilder& EngineBuilder::addRestartRuntimeGuard(std::string guard, bool value) {
  restart_runtime_guards_.emplace_back(std::move(guard), value);
  return *this;
}

#if defined(__APPLE__)
EngineBuilder& EngineBuilder::respectSystemProxySettings(bool value) {
  respect_system_proxy_settings_ = value;
  return *this;
}

EngineBuilder& EngineBuilder::setIosNetworkServiceType(int ios_network_service_type) {
  ios_network_service_type_ = ios_network_service_type;
  return *this;
}
#endif

std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> EngineBuilder::generateBootstrap() const {
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap =
      std::make_unique<envoy::config::bootstrap::v3::Bootstrap>();

  // Set up the HCM
  envoy::extensions::filters::network::http_connection_manager::v3::EnvoyMobileHttpConnectionManager
      api_listener_config;
  auto* hcm = api_listener_config.mutable_config();
  hcm->set_stat_prefix("hcm");
  hcm->set_server_header_transformation(
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
          PASS_THROUGH);
  hcm->mutable_stream_idle_timeout()->set_seconds(stream_idle_timeout_seconds_);
  auto* route_config = hcm->mutable_route_config();
  route_config->set_name("api_router");

  auto* api_service = route_config->add_virtual_hosts();
  api_service->set_name("api");
  api_service->set_include_attempt_count_in_response(true);
  api_service->add_domains("*");

  auto* route = api_service->add_routes();
  route->mutable_match()->set_prefix("/");
  route->add_request_headers_to_remove("x-forwarded-proto");
  route->add_request_headers_to_remove("x-envoy-mobile-cluster");
  route->mutable_per_request_buffer_limit_bytes()->set_value(4096);
  auto* route_to = route->mutable_route();
  route_to->set_cluster_header("x-envoy-mobile-cluster");
  route_to->mutable_timeout()->set_seconds(0);
  route_to->mutable_retry_policy()->mutable_per_try_idle_timeout()->set_seconds(
      per_try_idle_timeout_seconds_);
  auto* backoff = route_to->mutable_retry_policy()->mutable_retry_back_off();
  backoff->mutable_base_interval()->set_nanos(250000000);
  backoff->mutable_max_interval()->set_seconds(60);

  for (auto filter = native_filter_chain_.rbegin(); filter != native_filter_chain_.rend();
       ++filter) {
    auto* native_filter = hcm->add_http_filters();
    native_filter->set_name(filter->name_);
    if (!filter->textproto_typed_config_.empty()) {
#ifdef ENVOY_ENABLE_FULL_PROTOS
      Protobuf::TextFormat::ParseFromString((*filter).textproto_typed_config_,
                                            native_filter->mutable_typed_config());
      RELEASE_ASSERT(!native_filter->typed_config().DebugString().empty(),
                     "Failed to parse: " + (*filter).textproto_typed_config_);
#else
      RELEASE_ASSERT(
          native_filter->mutable_typed_config()->ParseFromString((*filter).textproto_typed_config_),
          "Failed to parse binary proto: " + (*filter).textproto_typed_config_);
#endif // !ENVOY_ENABLE_FULL_PROTOS
    } else {
      *native_filter->mutable_typed_config() = filter->typed_config_;
    }
  }

  // Set up the optional filters
  if (enable_http3_) {
    envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig cache_config;
    auto* cache_filter = hcm->add_http_filters();
    cache_filter->set_name("alternate_protocols_cache");
    cache_filter->mutable_typed_config()->PackFrom(cache_config);
  }

  if (gzip_decompression_filter_) {
    envoy::extensions::compression::gzip::decompressor::v3::Gzip gzip_config;
    gzip_config.mutable_window_bits()->set_value(15);
    envoy::extensions::filters::http::decompressor::v3::Decompressor decompressor_config;
    decompressor_config.mutable_decompressor_library()->set_name("gzip");
    decompressor_config.mutable_decompressor_library()->mutable_typed_config()->PackFrom(
        gzip_config);
    auto* common_request =
        decompressor_config.mutable_request_direction_config()->mutable_common_config();
    common_request->mutable_enabled()->mutable_default_value();
    common_request->mutable_enabled()->set_runtime_key("request_decompressor_enabled");
    decompressor_config.mutable_response_direction_config()
        ->mutable_common_config()
        ->set_ignore_no_transform_header(true);
    auto* gzip_filter = hcm->add_http_filters();
    gzip_filter->set_name("envoy.filters.http.decompressor");
    gzip_filter->mutable_typed_config()->PackFrom(decompressor_config);
  }
  if (brotli_decompression_filter_) {
    envoy::extensions::compression::brotli::decompressor::v3::Brotli brotli_config;
    envoy::extensions::filters::http::decompressor::v3::Decompressor decompressor_config;
    decompressor_config.mutable_decompressor_library()->set_name("text_optimized");
    decompressor_config.mutable_decompressor_library()->mutable_typed_config()->PackFrom(
        brotli_config);
    auto* common_request =
        decompressor_config.mutable_request_direction_config()->mutable_common_config();
    common_request->mutable_enabled()->mutable_default_value();
    common_request->mutable_enabled()->set_runtime_key("request_decompressor_enabled");
    decompressor_config.mutable_response_direction_config()
        ->mutable_common_config()
        ->set_ignore_no_transform_header(true);
    auto* brotli_filter = hcm->add_http_filters();
    brotli_filter->set_name("envoy.filters.http.decompressor");
    brotli_filter->mutable_typed_config()->PackFrom(decompressor_config);
  }
  if (socket_tagging_filter_) {
    envoymobile::extensions::filters::http::socket_tag::SocketTag tag_config;
    auto* tag_filter = hcm->add_http_filters();
    tag_filter->set_name("envoy.filters.http.socket_tag");
    tag_filter->mutable_typed_config()->PackFrom(tag_config);
  }

  // Set up the always-present filters
  envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration
      network_config;
  network_config.set_enable_drain_post_dns_refresh(enable_drain_post_dns_refresh_);
  network_config.set_enable_interface_binding(enable_interface_binding_);
  auto* network_filter = hcm->add_http_filters();
  network_filter->set_name("envoy.filters.http.network_configuration");
  network_filter->mutable_typed_config()->PackFrom(network_config);

  envoymobile::extensions::filters::http::local_error::LocalError local_config;
  auto* local_filter = hcm->add_http_filters();
  local_filter->set_name("envoy.filters.http.local_error");
  local_filter->mutable_typed_config()->PackFrom(local_config);

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig dfp_config;
  auto* dns_cache_config = dfp_config.mutable_dns_cache_config();
  dns_cache_config->set_name("base_dns_cache");
  dns_cache_config->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
  dns_cache_config->mutable_host_ttl()->set_seconds(86400);
  dns_cache_config->mutable_dns_min_refresh_rate()->set_seconds(dns_min_refresh_seconds_);
  dns_cache_config->mutable_dns_refresh_rate()->set_seconds(dns_refresh_seconds_);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(
      dns_failure_refresh_seconds_base_);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(
      dns_failure_refresh_seconds_max_);
  dns_cache_config->mutable_dns_query_timeout()->set_seconds(dns_query_timeout_seconds_);
  if (dns_cache_on_) {
    envoymobile::extensions::key_value::platform::PlatformKeyValueStoreConfig kv_config;
    kv_config.set_key("dns_persistent_cache");
    kv_config.mutable_save_interval()->set_seconds(dns_cache_save_interval_seconds_);
    kv_config.set_max_entries(100);
    dns_cache_config->mutable_key_value_config()->mutable_config()->set_name(
        "envoy.key_value.platform");
    dns_cache_config->mutable_key_value_config()
        ->mutable_config()
        ->mutable_typed_config()
        ->PackFrom(kv_config);
  }

#if defined(__APPLE__)
  envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig resolver_config;
  dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
      "envoy.network.dns_resolver.apple");
  dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
      resolver_config);
#else
  if (use_cares_) {
    envoy::extensions::network::dns_resolver::cares::v3::CaresDnsResolverConfig resolver_config;
    if (!cares_fallback_resolvers_.empty()) {
      for (const auto& [host, port] : cares_fallback_resolvers_) {
        auto* address = resolver_config.add_resolvers();
        address->mutable_socket_address()->set_address(host);
        address->mutable_socket_address()->set_port_value(port);
      }
      resolver_config.set_use_resolvers_as_fallback(true);
    }
    dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
        "envoy.network.dns_resolver.cares");
    dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
        resolver_config);
  } else {
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
        resolver_config;
    if (dns_num_retries_.has_value()) {
      resolver_config.mutable_num_retries()->set_value(*dns_num_retries_);
    }
    dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
        "envoy.network.dns_resolver.getaddrinfo");
    dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
        resolver_config);
  }
#endif

  for (const auto& [host, port] : dns_preresolve_hostnames_) {
    envoy::config::core::v3::SocketAddress* address = dns_cache_config->add_preresolve_hostnames();
    address->set_address(host);
    address->set_port_value(port);
  }

  auto* dfp_filter = hcm->add_http_filters();
  dfp_filter->set_name("envoy.filters.http.dynamic_forward_proxy");
  dfp_filter->mutable_typed_config()->PackFrom(dfp_config);

  auto* router_filter = hcm->add_http_filters();
  envoy::extensions::filters::http::router::v3::Router router_config;
  router_filter->set_name("envoy.router");
  router_filter->mutable_typed_config()->PackFrom(router_config);

  auto* static_resources = bootstrap->mutable_static_resources();

  // Finally create the base listener, and point it at the HCM.
  auto* base_listener = static_resources->add_listeners();
  base_listener->set_name("base_api_listener");
  auto* base_address = base_listener->mutable_address();
  base_address->mutable_socket_address()->set_protocol(envoy::config::core::v3::SocketAddress::TCP);
  base_address->mutable_socket_address()->set_address("0.0.0.0");
  base_address->mutable_socket_address()->set_port_value(10000);
  base_listener->mutable_per_connection_buffer_limit_bytes()->set_value(10485760);
  base_listener->mutable_api_listener()->mutable_api_listener()->PackFrom(api_listener_config);

  // Basic TLS config.
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_socket;
  if (!upstream_tls_sni_.empty()) {
    tls_socket.set_sni(upstream_tls_sni_);
  }
  tls_socket.mutable_common_tls_context()->mutable_tls_params()->set_tls_maximum_protocol_version(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLSv1_3);
  auto* validation = tls_socket.mutable_common_tls_context()->mutable_validation_context();
  if (enforce_trust_chain_verification_) {
    validation->set_trust_chain_verification(envoy::extensions::transport_sockets::tls::v3::
                                                 CertificateValidationContext::VERIFY_TRUST_CHAIN);
  } else {
    validation->set_trust_chain_verification(envoy::extensions::transport_sockets::tls::v3::
                                                 CertificateValidationContext::ACCEPT_UNTRUSTED);
  }

  if (platform_certificates_validation_on_) {
    envoy_mobile::extensions::cert_validator::platform_bridge::PlatformBridgeCertValidator
        validator;
    if (network_thread_priority_.has_value()) {
      validator.mutable_thread_priority()->set_value(*network_thread_priority_);
    }
    validation->mutable_custom_validator_config()->set_name(
        "envoy_mobile.cert_validator.platform_bridge_cert_validator");
    validation->mutable_custom_validator_config()->mutable_typed_config()->PackFrom(validator);
  } else {
    std::string certs;

    if (certs.empty()) {
      // The xDS builder doesn't supply root certs, so we'll use the certs packed with Envoy Mobile,
      // if the build config allows it.
      const char* inline_certs = ""
#ifndef EXCLUDE_CERTIFICATES
#include "library/common/config/certificates.inc"
#endif
                                 "";
      certs = inline_certs;
      // The certificates in certificates.inc are prefixed with 2 spaces per
      // line to be ingressed into YAML.
      absl::StrReplaceAll({{"\n  ", "\n"}}, &certs);
    }
    validation->mutable_trusted_ca()->set_inline_string(certs);
  }
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
      ssl_proxy_socket;
  ssl_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  ssl_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_socket);

  envoy::config::core::v3::TransportSocket base_tls_socket;
  base_tls_socket.set_name("envoy.transport_sockets.http_11_proxy");
  base_tls_socket.mutable_typed_config()->PackFrom(ssl_proxy_socket);

  envoy::extensions::upstreams::http::v3::HttpProtocolOptions h2_protocol_options;
  h2_protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();

  // Base cluster config (DFP cluster config)
  auto* base_cluster = static_resources->add_clusters();
  envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig base_cluster_config;
  envoy::config::cluster::v3::Cluster::CustomClusterType base_cluster_type;
  base_cluster_config.mutable_dns_cache_config()->CopyFrom(*dns_cache_config);
  base_cluster_type.set_name("envoy.clusters.dynamic_forward_proxy");
  base_cluster_type.mutable_typed_config()->PackFrom(base_cluster_config);

  auto* upstream_opts = base_cluster->mutable_upstream_connection_options();
  upstream_opts->set_set_local_interface_name_on_upstream_connections(true);
  upstream_opts->mutable_tcp_keepalive()->mutable_keepalive_interval()->set_value(5);
  upstream_opts->mutable_tcp_keepalive()->mutable_keepalive_probes()->set_value(1);
  upstream_opts->mutable_tcp_keepalive()->mutable_keepalive_time()->set_value(10);

  auto* circuit_breaker_settings = base_cluster->mutable_circuit_breakers();
  auto* breaker1 = circuit_breaker_settings->add_thresholds();
  breaker1->set_priority(envoy::config::core::v3::RoutingPriority::DEFAULT);
  breaker1->mutable_retry_budget()->mutable_budget_percent()->set_value(100);
  breaker1->mutable_retry_budget()->mutable_min_retry_concurrency()->set_value(0xffffffff);
  auto* breaker2 = circuit_breaker_settings->add_per_host_thresholds();
  breaker2->set_priority(envoy::config::core::v3::RoutingPriority::DEFAULT);
  breaker2->mutable_max_connections()->set_value(max_connections_per_host_);

  envoy::extensions::upstreams::http::v3::HttpProtocolOptions alpn_options;
  alpn_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
  alpn_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
  auto* h2_options = alpn_options.mutable_auto_config()->mutable_http2_protocol_options();
  if (h2_connection_keepalive_idle_interval_milliseconds_ > 1000) {
    h2_options->mutable_connection_keepalive()->mutable_connection_idle_interval()->set_seconds(
        h2_connection_keepalive_idle_interval_milliseconds_ / 1000);
  } else {
    h2_options->mutable_connection_keepalive()->mutable_connection_idle_interval()->set_nanos(
        h2_connection_keepalive_idle_interval_milliseconds_ * 1000 * 1000);
  }
  h2_options->mutable_connection_keepalive()->mutable_timeout()->set_seconds(
      h2_connection_keepalive_timeout_seconds_);
  h2_options->mutable_max_concurrent_streams()->set_value(100);
  h2_options->mutable_initial_stream_window_size()->set_value(initial_stream_window_size_);
  h2_options->mutable_initial_connection_window_size()->set_value(initial_connection_window_size_);

  envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig
      preserve_case_config;
  preserve_case_config.set_forward_reason_phrase(false);
  preserve_case_config.set_formatter_type_on_envoy_headers(
      envoy::extensions::http::header_formatters::preserve_case::v3::PreserveCaseFormatterConfig::
          DEFAULT);

  auto* h1_options = alpn_options.mutable_auto_config()->mutable_http_protocol_options();
  auto* formatter = h1_options->mutable_header_key_format()->mutable_stateful_formatter();
  formatter->set_name("preserve_case");
  formatter->mutable_typed_config()->PackFrom(preserve_case_config);

  // Base cluster
  base_cluster->set_name("base");
  base_cluster->mutable_connect_timeout()->set_seconds(connect_timeout_seconds_);
  base_cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  (*base_cluster->mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(alpn_options);
  base_cluster->mutable_cluster_type()->CopyFrom(base_cluster_type);
  base_cluster->mutable_transport_socket()->set_name("envoy.transport_sockets.http_11_proxy");
  base_cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(ssl_proxy_socket);

  // Base clear-text cluster set up
  envoy::extensions::transport_sockets::raw_buffer::v3::RawBuffer raw_buffer;
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
      cleartext_proxy_socket;
  cleartext_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(raw_buffer);
  cleartext_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.raw_buffer");
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions h1_protocol_options;
  h1_protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
  h1_protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
  h1_protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options()->CopyFrom(
      *alpn_options.mutable_auto_config()->mutable_http_protocol_options());

  // Base clear-text cluster.
  auto* base_clear = static_resources->add_clusters();
  base_clear->set_name("base_clear");
  base_clear->mutable_connect_timeout()->set_seconds(connect_timeout_seconds_);
  base_clear->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  base_clear->mutable_cluster_type()->CopyFrom(base_cluster_type);
  base_clear->mutable_transport_socket()->set_name("envoy.transport_sockets.http_11_proxy");
  base_clear->mutable_transport_socket()->mutable_typed_config()->PackFrom(cleartext_proxy_socket);
  base_clear->mutable_upstream_connection_options()->CopyFrom(
      *base_cluster->mutable_upstream_connection_options());
  base_clear->mutable_circuit_breakers()->CopyFrom(*base_cluster->mutable_circuit_breakers());
  (*base_clear->mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(h1_protocol_options);

  // Edit and re-pack
  tls_socket.mutable_common_tls_context()->add_alpn_protocols("h2");
  ssl_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_socket);

  // Edit base cluster to be an HTTP/3 cluster.
  if (enable_http3_) {
    envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport h3_inner_socket;
    tls_socket.mutable_common_tls_context()->mutable_alpn_protocols()->Clear();
    h3_inner_socket.mutable_upstream_tls_context()->CopyFrom(tls_socket);
    envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
        h3_proxy_socket;
    h3_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_inner_socket);
    h3_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.quic");
    alpn_options.mutable_auto_config()
        ->mutable_http3_protocol_options()
        ->mutable_quic_protocol_options()
        ->set_connection_options(http3_connection_options_);
    alpn_options.mutable_auto_config()
        ->mutable_http3_protocol_options()
        ->mutable_quic_protocol_options()
        ->set_client_connection_options(http3_client_connection_options_);
    alpn_options.mutable_auto_config()
        ->mutable_http3_protocol_options()
        ->mutable_quic_protocol_options()
        ->mutable_initial_stream_window_size()
        ->set_value(initial_stream_window_size_);
    alpn_options.mutable_auto_config()
        ->mutable_http3_protocol_options()
        ->mutable_quic_protocol_options()
        ->mutable_initial_connection_window_size()
        ->set_value(initial_connection_window_size_);
    alpn_options.mutable_auto_config()->mutable_alternate_protocols_cache_options()->set_name(
        "default_alternate_protocols_cache");
    for (const auto& [host, port] : quic_hints_) {
      auto* entry = alpn_options.mutable_auto_config()
                        ->mutable_alternate_protocols_cache_options()
                        ->add_prepopulated_entries();
      entry->set_hostname(host);
      entry->set_port(port);
    }
    for (const auto& suffix : quic_suffixes_) {
      alpn_options.mutable_auto_config()
          ->mutable_alternate_protocols_cache_options()
          ->add_canonical_suffixes(suffix);
    }

    if (num_timeouts_to_trigger_port_migration_ > 0) {
      alpn_options.mutable_auto_config()
          ->mutable_http3_protocol_options()
          ->mutable_quic_protocol_options()
          ->mutable_num_timeouts_to_trigger_port_migration()
          ->set_value(num_timeouts_to_trigger_port_migration_);
    }

    alpn_options.mutable_auto_config()
        ->mutable_http3_protocol_options()
        ->mutable_quic_protocol_options()
        ->mutable_idle_network_timeout()
        ->set_seconds(quic_connection_idle_timeout_seconds_);

    base_cluster->mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_proxy_socket);
    (*base_cluster->mutable_typed_extension_protocol_options())
        ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
            .PackFrom(alpn_options);

    // Set the upstream connections UDP socket receive buffer size. The operating system defaults
    // are usually too small for QUIC.
    envoy::config::core::v3::SocketOption* udp_rcv_buf_sock_opt =
        base_cluster->mutable_upstream_bind_config()->add_socket_options();
    udp_rcv_buf_sock_opt->set_name(SO_RCVBUF);
    udp_rcv_buf_sock_opt->set_level(SOL_SOCKET);
    udp_rcv_buf_sock_opt->set_int_value(udp_socket_receive_buffer_size_);
    // Only apply the socket option to the datagram socket.
    udp_rcv_buf_sock_opt->mutable_type()->mutable_datagram();
    udp_rcv_buf_sock_opt->set_description(
        absl::StrCat("UDP SO_RCVBUF = ", udp_socket_receive_buffer_size_, " bytes"));

    envoy::config::core::v3::SocketOption* udp_snd_buf_sock_opt =
        base_cluster->mutable_upstream_bind_config()->add_socket_options();
    udp_snd_buf_sock_opt->set_name(SO_SNDBUF);
    udp_snd_buf_sock_opt->set_level(SOL_SOCKET);
    udp_snd_buf_sock_opt->set_int_value(udp_socket_send_buffer_size_);
    // Only apply the socket option to the datagram socket.
    udp_snd_buf_sock_opt->mutable_type()->mutable_datagram();
    udp_snd_buf_sock_opt->set_description(
        absl::StrCat("UDP SO_SNDBUF = ", udp_socket_send_buffer_size_, " bytes"));
    // Set the network service type on iOS, if supplied.
#if defined(__APPLE__)
    if (ios_network_service_type_ > 0) {
      envoy::config::core::v3::SocketOption* net_svc_sock_opt =
          base_cluster->mutable_upstream_bind_config()->add_socket_options();
      net_svc_sock_opt->set_name(SO_NET_SERVICE_TYPE);
      net_svc_sock_opt->set_level(SOL_SOCKET);
      net_svc_sock_opt->set_int_value(ios_network_service_type_);
      net_svc_sock_opt->set_description(
          absl::StrCat("SO_NET_SERVICE_TYPE = ", ios_network_service_type_));
    }
#endif
  }

  // Set up stats.
  auto* list = bootstrap->mutable_stats_config()->mutable_stats_matcher()->mutable_inclusion_list();
  list->add_patterns()->set_prefix("cluster.base.upstream_rq_");
  list->add_patterns()->set_prefix("cluster.stats.upstream_rq_");
  list->add_patterns()->set_prefix("cluster.base.upstream_cx_");
  list->add_patterns()->set_prefix("cluster.stats.upstream_cx_");
  list->add_patterns()->set_exact("cluster.base.http2.keepalive_timeout");
  list->add_patterns()->set_exact("cluster.base.upstream_http3_broken");
  list->add_patterns()->set_exact("cluster.stats.http2.keepalive_timeout");
  list->add_patterns()->set_prefix("http.hcm.downstream_rq_");
  list->add_patterns()->set_prefix("http.hcm.decompressor.");
  list->add_patterns()->set_prefix("pulse.");
  list->add_patterns()->set_prefix("runtime.load_success");
  list->add_patterns()->set_prefix("dns_cache");
  list->add_patterns()->mutable_safe_regex()->set_regex(
      "^vhost\\.[\\w]+\\.vcluster\\.[\\w]+?\\.upstream_rq_(?:[12345]xx|[3-5][0-9][0-9]|retry|"
      "total)");
  list->add_patterns()->set_contains("quic_connection_close_error_code");
  list->add_patterns()->set_contains("quic_reset_stream_error_code");
  bootstrap->mutable_stats_config()->mutable_use_all_default_tags()->set_value(false);

  // Set up watchdog
  auto* watchdog = bootstrap->mutable_watchdogs();
  watchdog->mutable_main_thread_watchdog()->mutable_megamiss_timeout()->set_seconds(60);
  watchdog->mutable_main_thread_watchdog()->mutable_miss_timeout()->set_seconds(60);
  watchdog->mutable_worker_watchdog()->mutable_megamiss_timeout()->set_seconds(60);
  watchdog->mutable_worker_watchdog()->mutable_miss_timeout()->set_seconds(60);

  // Set up node
  auto* node = bootstrap->mutable_node();
  node->set_id("envoy-mobile");
  node->set_cluster("envoy-mobile");
  ProtobufWkt::Struct& metadata = *node->mutable_metadata();
  (*metadata.mutable_fields())["app_id"].set_string_value(app_id_);
  (*metadata.mutable_fields())["app_version"].set_string_value(app_version_);
  (*metadata.mutable_fields())["device_os"].set_string_value(device_os_);

  // Set up runtime.
  auto* runtime = bootstrap->mutable_layered_runtime()->add_layers();
  runtime->set_name("static_layer_0");
  ProtobufWkt::Struct envoy_layer;
  ProtobufWkt::Struct& runtime_values =
      *(*envoy_layer.mutable_fields())["envoy"].mutable_struct_value();
  ProtobufWkt::Struct& reloadable_features =
      *(*runtime_values.mutable_fields())["reloadable_features"].mutable_struct_value();
  for (auto& guard_and_value : runtime_guards_) {
    (*reloadable_features.mutable_fields())[guard_and_value.first].set_bool_value(
        guard_and_value.second);
  }
  (*reloadable_features.mutable_fields())["always_use_v6"].set_bool_value(always_use_v6_);
  (*reloadable_features.mutable_fields())["prefer_quic_client_udp_gro"].set_bool_value(
      use_gro_if_available_);
  ProtobufWkt::Struct& restart_features =
      *(*runtime_values.mutable_fields())["restart_features"].mutable_struct_value();
  for (auto& guard_and_value : restart_runtime_guards_) {
    (*restart_features.mutable_fields())[guard_and_value.first].set_bool_value(
        guard_and_value.second);
  }

  (*runtime_values.mutable_fields())["disallow_global_stats"].set_bool_value(true);
  (*runtime_values.mutable_fields())["enable_dfp_dns_trace"].set_bool_value(true);
  ProtobufWkt::Struct& overload_values =
      *(*envoy_layer.mutable_fields())["overload"].mutable_struct_value();
  (*overload_values.mutable_fields())["global_downstream_max_connections"].set_string_value(
      "4294967295");
  runtime->mutable_static_layer()->MergeFrom(envoy_layer);

  bootstrap->mutable_typed_dns_resolver_config()->CopyFrom(
      *dns_cache_config->mutable_typed_dns_resolver_config());

  bootstrap->mutable_dynamic_resources();

  envoy::config::listener::v3::ApiListenerManager api;
  auto* listener_manager = bootstrap->mutable_listener_manager();
  listener_manager->mutable_typed_config()->PackFrom(api);
  listener_manager->set_name("envoy.listener_manager_impl.api");

  return bootstrap;
}

EngineSharedPtr EngineBuilder::build() {
  InternalEngine* envoy_engine =
      new InternalEngine(std::move(callbacks_), std::move(logger_), std::move(event_tracker_),
                         network_thread_priority_);

  for (const auto& [name, store] : key_value_stores_) {
    // TODO(goaway): This leaks, but it's tied to the life of the engine.
    if (!Api::External::retrieveApi(name, true)) {
      auto* api = new envoy_kv_store();
      *api = store->asEnvoyKeyValueStore();
      Envoy::Api::External::registerApi(name.c_str(), api);
    }
  }

  for (const auto& [name, accessor] : string_accessors_) {
    // TODO(RyanTheOptimist): This leaks, but it's tied to the life of the engine.
    if (!Api::External::retrieveApi(name, true)) {
      auto* api = new envoy_string_accessor();
      *api = StringAccessor::asEnvoyStringAccessor(accessor);
      Envoy::Api::External::registerApi(name.c_str(), api);
    }
  }

#if defined(__APPLE__)
  if (respect_system_proxy_settings_) {
    registerAppleProxyResolver();
  }
#endif

  Engine* engine = new Engine(envoy_engine);

  auto options = std::make_shared<Envoy::OptionsImplBase>();
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap = generateBootstrap();
  if (bootstrap) {
    options->setConfigProto(std::move(bootstrap));
  }
  options->setLogLevel(static_cast<spdlog::level::level_enum>(log_level_));
  options->setConcurrency(1);
  envoy_engine->run(options);

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy
