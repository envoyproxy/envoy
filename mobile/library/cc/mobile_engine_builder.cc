#include "library/cc/mobile_engine_builder.h"

#include <stdint.h>
#include <sys/socket.h>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/socket_option.pb.h"
#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/filters/http/router/v3/router.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/early_data/v3/default_early_data_policy.pb.h"

#if defined(__APPLE__)
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver.pb.h"
#endif
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "source/common/http/matching/inputs.h"
#include "envoy/config/core/v3/base.pb.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "source/common/runtime/runtime_features.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/debugging/leak_check.h"
#include "fmt/core.h"
#include "library/common/internal_engine.h"
#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge.pb.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"
#include "library/common/extensions/filters/http/local_error/filter.pb.h"
#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/extensions/filters/http/socket_tag/filter.pb.h"
#include "library/common/extensions/key_value/platform/platform.pb.h"
#include "library/common/extensions/quic_packet_writer/platform/platform_packet_writer.pb.h"

#if defined(__APPLE__)
#include "library/common/network/apple_proxy_resolution.h"
#endif

namespace Envoy {
namespace Platform {

MobileEngineBuilder::MobileEngineBuilder() {}

MobileEngineBuilder& MobileEngineBuilder::setNetworkThreadPriority(int thread_priority) {
  network_thread_priority_ = thread_priority;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addConnectTimeoutSeconds(int connect_timeout_seconds) {
  connect_timeout_seconds_ = connect_timeout_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addDnsRefreshSeconds(int dns_refresh_seconds) {
  dns_refresh_seconds_ = dns_refresh_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addDnsMinRefreshSeconds(int dns_min_refresh_seconds) {
  dns_min_refresh_seconds_ = dns_min_refresh_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addDnsFailureRefreshSeconds(int base, int max) {
  dns_failure_refresh_seconds_base_ = base;
  dns_failure_refresh_seconds_max_ = max;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds) {
  dns_query_timeout_seconds_ = dns_query_timeout_seconds;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setDisableDnsRefreshOnFailure(bool disable_dns_refresh_on_failure) {
  disable_dns_refresh_on_failure_ = disable_dns_refresh_on_failure;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setDisableDnsRefreshOnNetworkChange(
    bool disable_dns_refresh_on_network_change) {
  disable_dns_refresh_on_network_change_ = disable_dns_refresh_on_network_change;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setDnsNumRetries(uint32_t dns_num_retries) {
  dns_num_retries_ = dns_num_retries;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setGetaddrinfoNumThreads(uint32_t num_threads) {
  getaddrinfo_num_threads_ = num_threads;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::addDnsPreresolveHostnames(const std::vector<std::string>& hostnames) {
  dns_preresolve_hostnames_.clear();
  for (const std::string& hostname : hostnames) {
    dns_preresolve_hostnames_.push_back({hostname /* host */, 443 /* port */});
  }
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setDnsResolver(
    const envoy::config::core::v3::TypedExtensionConfig& dns_resolver_config) {
  dns_resolver_config_ = dns_resolver_config;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setAdditionalSocketOptions(
    const std::vector<envoy::config::core::v3::SocketOption>& socket_options) {
  socket_options_ = socket_options;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addMaxConnectionsPerHost(int max_connections_per_host) {
  max_connections_per_host_ = max_connections_per_host;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addH2ConnectionKeepaliveIdleIntervalMilliseconds(
    int h2_connection_keepalive_idle_interval_milliseconds) {
  h2_connection_keepalive_idle_interval_milliseconds_ =
      h2_connection_keepalive_idle_interval_milliseconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addH2ConnectionKeepaliveTimeoutSeconds(
    int h2_connection_keepalive_timeout_seconds) {
  h2_connection_keepalive_timeout_seconds_ = h2_connection_keepalive_timeout_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addKeyValueStore(std::string name,
                                                           KeyValueStoreSharedPtr key_value_store) {
  key_value_stores_[std::move(name)] = std::move(key_value_store);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setAppVersion(std::string app_version) {
  app_version_ = std::move(app_version);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setAppId(std::string app_id) {
  app_id_ = std::move(app_id);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setDeviceOs(std::string device_os) {
  device_os_ = std::move(device_os);
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds) {
  per_try_idle_timeout_seconds_ = per_try_idle_timeout_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableGzipDecompression(bool gzip_decompression_on) {
  gzip_decompression_filter_ = gzip_decompression_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableBrotliDecompression(bool brotli_decompression_on) {
  brotli_decompression_filter_ = brotli_decompression_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableSocketTagging(bool socket_tagging_on) {
  socket_tagging_filter_ = socket_tagging_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableHttp3(bool http3_on) {
  enable_http3_ = http3_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableEarlyData(bool early_data_on) {
  enable_early_data_ = early_data_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableScone(bool enable) {
  scone_enabled_ = enable;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addQuicConnectionOption(std::string option) {
  quic_connection_options_.push_back(std::move(option));
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addQuicClientConnectionOption(std::string option) {
  quic_client_connection_options_.push_back(std::move(option));
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setHttp3ConnectionOptions(std::string options) {
  http3_connection_options_ = std::move(options);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setHttp3ClientConnectionOptions(std::string options) {
  http3_client_connection_options_ = std::move(options);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addQuicHint(std::string host, int port) {
  quic_hints_.emplace_back(std::move(host), port);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addQuicCanonicalSuffix(std::string suffix) {
  quic_suffixes_.emplace_back(std::move(suffix));
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setNumTimeoutsToTriggerPortMigration(int num_timeouts) {
  num_timeouts_to_trigger_port_migration_ = num_timeouts;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableInterfaceBinding(bool interface_binding_on) {
  enable_interface_binding_ = interface_binding_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setUdpSocketReceiveBufferSize(int32_t size) {
  udp_socket_receive_buffer_size_ = size;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setUdpSocketSendBufferSize(int32_t size) {
  udp_socket_send_buffer_size_ = size;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on) {
  enable_drain_post_dns_refresh_ = drain_post_dns_refresh_on;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::enforceTrustChainVerification(bool trust_chain_verification_on) {
  enforce_trust_chain_verification_ = trust_chain_verification_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setUpstreamTlsSni(std::string sni) {
  upstream_tls_sni_ = std::move(sni);
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setQuicConnectionIdleTimeoutSeconds(int quic_connection_idle_timeout_seconds) {
  quic_connection_idle_timeout_seconds_ = quic_connection_idle_timeout_seconds;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setKeepAliveInitialIntervalMilliseconds(int keepalive_initial_interval_ms) {
  keepalive_initial_interval_ms_ = keepalive_initial_interval_ms;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setMaxConcurrentStreams(int max_concurrent_streams) {
  max_concurrent_streams_ = max_concurrent_streams;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enablePlatformCertificatesValidation(
    bool platform_certificates_validation_on) {
  if (useWorkerThread()) {
    return *this;
  }
  platform_certificates_validation_on_ = platform_certificates_validation_on;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableDnsCache(bool dns_cache_on,
                                                         int save_interval_seconds) {
  dns_cache_on_ = dns_cache_on;
  dns_cache_save_interval_seconds_ = save_interval_seconds;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addStringAccessor(std::string name,
                                                            StringAccessorSharedPtr accessor) {
  string_accessors_[std::move(name)] = std::move(accessor);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addNativeFilter(std::string name,
                                                          std::string typed_config) {
  native_filter_chain_.emplace_back(NativeFilterConfig(std::move(name), std::move(typed_config)));
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addNativeFilter(const std::string& name,
                                                          const Protobuf::Any& typed_config) {
  native_filter_chain_.push_back(NativeFilterConfig(name, typed_config));
  return *this;
}

#if defined(__APPLE__)
MobileEngineBuilder&
MobileEngineBuilder::enableNetworkChangeMonitor(bool network_change_monitor_on) {
  enable_network_change_monitor_ = network_change_monitor_on;
  return *this;
}
#endif

std::string MobileEngineBuilder::nativeNameToConfig(absl::string_view name) {
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
  Protobuf::Any any_config;
  any_config.set_type_url(
      "type.googleapis.com/envoymobile.extensions.filters.http.platform_bridge.PlatformBridge");
  any_config.set_value(ret);
  any_config.SerializeToString(&ret);
  return ret;
#endif
}

MobileEngineBuilder& MobileEngineBuilder::addPlatformFilter(const std::string& name) {
  addNativeFilter("envoy.filters.http.platform_bridge", nativeNameToConfig(name));
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::enableWorkerThread(bool use_worker_thread) {
  Platform::EngineBuilderBase<MobileEngineBuilder>::enableWorkerThread(use_worker_thread);
  if (useWorkerThread()) {
    platform_certificates_validation_on_ = false;
#ifdef __APPLE__
    respect_system_proxy_settings_ = false;
#endif
  }
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::addRuntimeGuard(std::string guard, bool value) {
  addReloadableRuntimeGuard(std::move(guard), value);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setNodeId(std::string node_id) {
  node_id_ = std::move(node_id);
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setNodeLocality(std::string region, std::string zone,
                                                          std::string sub_zone) {
  node_locality_ = {std::move(region), std::move(zone), std::move(sub_zone)};
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setNodeMetadata(Protobuf::Struct node_metadata) {
  node_metadata_ = std::move(node_metadata);
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setUseQuicPlatformPacketWriter(bool use_quic_platform_packet_writer) {
  use_quic_platform_packet_writer_ = use_quic_platform_packet_writer;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::enableQuicConnectionMigration(bool quic_connection_migration_on) {
  enable_quic_connection_migration_ = quic_connection_migration_on;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setMigrateIdleQuicConnection(bool migrate_idle_quic_connection) {
  migrate_idle_quic_connection_ = migrate_idle_quic_connection;
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setMaxIdleTimeBeforeQuicMigrationSeconds(
    int max_idle_time_before_quic_migration) {
  max_idle_time_before_quic_migration_seconds_ = max_idle_time_before_quic_migration;
  return *this;
}

MobileEngineBuilder&
MobileEngineBuilder::setMaxTimeOnNonDefaultNetworkSeconds(int max_time_on_non_default_network) {
  max_time_on_non_default_network_seconds_ = max_time_on_non_default_network;
  return *this;
}

#if defined(__APPLE__)
MobileEngineBuilder& MobileEngineBuilder::respectSystemProxySettings(bool value,
                                                                     int refresh_interval_secs) {
  if (useWorkerThread()) {
    return *this;
  }
  respect_system_proxy_settings_ = value;
  if (refresh_interval_secs > 0) {
    proxy_settings_refresh_interval_secs_ = refresh_interval_secs;
  }
  return *this;
}

MobileEngineBuilder& MobileEngineBuilder::setIosNetworkServiceType(int ios_network_service_type) {
  ios_network_service_type_ = ios_network_service_type;
  return *this;
}
#endif

#ifdef ENVOY_MOBILE_XDS

MobileEngineBuilder& MobileEngineBuilder::setXds(XdsBuilder xds_builder) {
  xds_builder_ = std::move(xds_builder);
  dns_preresolve_hostnames_.push_back(
      {xds_builder_->xds_server_address_ /* host */, xds_builder_->xds_server_port_ /* port */});
  return *this;
}
#endif // ENVOY_MOBILE_XDS

absl::Status MobileEngineBuilder::configureRouteConfig(
    envoy::config::route::v3::RouteConfiguration* route_config) {
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

  if (!enable_early_data_) {
    auto* early_data = route_to->mutable_early_data_policy();
    early_data->set_name("envoy.route.early_data_policy.default");
    ::envoy::extensions::early_data::v3::DefaultEarlyDataPolicy config;
    early_data->mutable_typed_config()->PackFrom(config);
  }
  return absl::OkStatus();
}

absl::Status MobileEngineBuilder::configureHttpFilters(
    std::function<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter*()>
        add_filter) {
  for (auto filter = native_filter_chain_.rbegin(); filter != native_filter_chain_.rend();
       ++filter) {
    auto* native_filter = add_filter();
    native_filter->set_name(filter->name_);
    if (!filter->textproto_typed_config_.empty()) {
#if ENVOY_ENABLE_FULL_PROTOS
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

  if (enable_http3_) {
    envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig cache_config;
    auto* cache_filter = add_filter();
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
    auto* gzip_filter = add_filter();
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
    auto* brotli_filter = add_filter();
    brotli_filter->set_name("envoy.filters.http.decompressor");
    brotli_filter->mutable_typed_config()->PackFrom(decompressor_config);
  }
  if (socket_tagging_filter_) {
    envoymobile::extensions::filters::http::socket_tag::SocketTag tag_config;
    auto* tag_filter = add_filter();
    tag_filter->set_name("envoy.filters.http.socket_tag");
    tag_filter->mutable_typed_config()->PackFrom(tag_config);
  }

  envoymobile::extensions::filters::http::network_configuration::NetworkConfiguration
      network_config;
  network_config.set_enable_drain_post_dns_refresh(enable_drain_post_dns_refresh_);
  network_config.set_enable_interface_binding(enable_interface_binding_);
  auto* network_filter = add_filter();
  network_filter->set_name("envoy.filters.http.network_configuration");
  network_filter->mutable_typed_config()->PackFrom(network_config);

  envoymobile::extensions::filters::http::local_error::LocalError local_config;
  auto* local_filter = add_filter();
  local_filter->set_name("envoy.filters.http.local_error");
  local_filter->mutable_typed_config()->PackFrom(local_config);

  envoy::extensions::filters::http::dynamic_forward_proxy::v3::FilterConfig dfp_config;
  configureDnsCache(dfp_config.mutable_dns_cache_config());
  auto* dfp_filter = add_filter();
  dfp_filter->set_name("envoy.filters.http.dynamic_forward_proxy");
  dfp_filter->mutable_typed_config()->PackFrom(dfp_config);
  return absl::OkStatus();
}

void MobileEngineBuilder::configureDnsCache(
    envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig* dns_cache_config) const {
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
  dns_cache_config->set_disable_dns_refresh_on_failure(disable_dns_refresh_on_failure_);
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

  if (dns_resolver_config_.has_value()) {
    *dns_cache_config->mutable_typed_dns_resolver_config() = *dns_resolver_config_;
  } else {
#if defined(__APPLE__)
    envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig resolver_config;
    dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
        "envoy.network.dns_resolver.apple");
    dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
        resolver_config);
#else
    envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
        resolver_config;
    if (dns_num_retries_.has_value()) {
      resolver_config.mutable_num_retries()->set_value(*dns_num_retries_);
    }
    resolver_config.mutable_num_resolver_threads()->set_value(getaddrinfo_num_threads_);
    dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
        "envoy.network.dns_resolver.getaddrinfo");
    dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
        resolver_config);
#endif
  }

  for (const auto& [host, port] : dns_preresolve_hostnames_) {
    envoy::config::core::v3::SocketAddress* address = dns_cache_config->add_preresolve_hostnames();
    address->set_address(host);
    address->set_port_value(port);
  }
}

absl::Status MobileEngineBuilder::configureStaticClusters(
    Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster>* clusters) {
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
#ifdef ENVOY_MOBILE_XDS
    if (xds_builder_ && !xds_builder_->ssl_root_certs_.empty()) {
      certs = xds_builder_->ssl_root_certs_;
    }
#endif // ENVOY_MOBILE_XDS
    if (certs.empty()) {
      const char* inline_certs = ""
#ifndef EXCLUDE_CERTIFICATES
#include "library/common/config/certificates.inc"
#endif
                                 "";
      certs = inline_certs;
      absl::StrReplaceAll({{"\n  ", "\n"}}, &certs);
    }
    if (!certs.empty()) {
      validation->mutable_trusted_ca()->set_inline_string(certs);
    }
  }
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
      ssl_proxy_socket;
  ssl_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  ssl_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_socket);

  envoy::extensions::upstreams::http::v3::HttpProtocolOptions h2_protocol_options;
  h2_protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();

  envoy::config::cluster::v3::Cluster* base_cluster = clusters->Add();
  envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig base_cluster_config;
  envoy::config::cluster::v3::Cluster::CustomClusterType base_cluster_type;
  configureDnsCache(base_cluster_config.mutable_dns_cache_config());
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
  if (max_concurrent_streams_ > 0) {
    h2_options->mutable_max_concurrent_streams()->set_value(max_concurrent_streams_);
  }

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
  envoy::config::cluster::v3::Cluster* base_clear = clusters->Add();
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

    auto* quic_protocol_options = alpn_options.mutable_auto_config()
                                      ->mutable_http3_protocol_options()
                                      ->mutable_quic_protocol_options();
    if (!quic_connection_options_.empty()) {
      quic_protocol_options->set_connection_options(absl::StrJoin(quic_connection_options_, ","));
    } else {
      quic_protocol_options->set_connection_options(http3_connection_options_);
    }
    if (!quic_client_connection_options_.empty()) {
      quic_protocol_options->set_client_connection_options(
          absl::StrJoin(quic_client_connection_options_, ","));
    } else {
      quic_protocol_options->set_client_connection_options(http3_client_connection_options_);
    }
    quic_protocol_options->mutable_initial_stream_window_size()->set_value(
        initial_stream_window_size_);
    quic_protocol_options->mutable_initial_connection_window_size()->set_value(
        initial_connection_window_size_);
    quic_protocol_options->mutable_idle_network_timeout()->set_seconds(
        quic_connection_idle_timeout_seconds_);
    if (num_timeouts_to_trigger_port_migration_ > 0) {
      quic_protocol_options->mutable_num_timeouts_to_trigger_port_migration()->set_value(
          num_timeouts_to_trigger_port_migration_);
    }
    if (keepalive_initial_interval_ms_ > 0) {
      quic_protocol_options->mutable_connection_keepalive()->mutable_initial_interval()->set_nanos(
          keepalive_initial_interval_ms_ * 1000 * 1000);
    }
    if (max_concurrent_streams_ > 0) {
      quic_protocol_options->mutable_max_concurrent_streams()->set_value(max_concurrent_streams_);
    }
    if (enable_quic_connection_migration_) {
      auto* migration_setting = quic_protocol_options->mutable_connection_migration();
      if (migrate_idle_quic_connection_) {
        auto* migrate_idle_connections = migration_setting->mutable_migrate_idle_connections();
        if (max_idle_time_before_quic_migration_seconds_ > 0) {
          migrate_idle_connections->mutable_max_idle_time_before_migration()->set_seconds(
              max_idle_time_before_quic_migration_seconds_);
        }
      }
      if (max_time_on_non_default_network_seconds_ > 0) {
        migration_setting->mutable_max_time_on_non_default_network()->set_seconds(
            max_time_on_non_default_network_seconds_);
      }
    }

    if (scone_enabled_) {
      quic_protocol_options->mutable_enable_scone()->set_value(true);
    }

    if (use_quic_platform_packet_writer_ || enable_quic_connection_migration_) {
      envoy_mobile::extensions::quic_packet_writer::platform::QuicPlatformPacketWriterConfig
          writer_config;
      quic_protocol_options->mutable_client_packet_writer()->mutable_typed_config()->PackFrom(
          writer_config);
      quic_protocol_options->mutable_client_packet_writer()->set_name(
          "envoy.quic.packet_writer.platform");
    }

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
    for (const auto& socket_option : socket_options_) {
      envoy::config::core::v3::SocketOption* sock_opt =
          base_cluster->mutable_upstream_bind_config()->add_socket_options();
      sock_opt->CopyFrom(socket_option);
    }
  }
  return absl::OkStatus();
}

void MobileEngineBuilder::addStatsInclusionStringMatchers() {
  auto add_prefix = [this](const std::string& prefix) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.set_prefix(prefix);
    addStatsInclusionPattern(std::move(matcher));
  };
  auto add_regex = [this](const std::string& regex) {
    envoy::type::matcher::v3::StringMatcher matcher;
    matcher.mutable_safe_regex()->set_regex(regex);
    addStatsInclusionPattern(std::move(matcher));
  };
  add_prefix("cluster.base.upstream_rq_");
  add_prefix("cluster.stats.upstream_rq_");
  add_prefix("cluster.base.upstream_cx_");
  add_prefix("cluster.stats.upstream_cx_");
  add_regex("^cluster\\.base\\.http2\\.keepalive_timeout$");
  add_regex("^cluster\\.base\\.upstream_http3_broken$");
  add_regex("^cluster\\.stats\\.http2\\.keepalive_timeout$");
  add_prefix("http.hcm.downstream_rq_");
  add_prefix("http.hcm.decompressor.");
  add_prefix("pulse.");
  add_prefix("runtime.load_success");
  add_prefix("dns_cache");
  add_regex("^vhost\\.[\\w]+\\.vcluster\\.[\\w]+?\\.upstream_rq_(?:[12345]xx|[3-5][0-9][0-9]|retry|"
            "total)");
  add_regex(".*quic_connection_close_error_code.*");
  add_regex(".*quic_reset_stream_error_code.*");
}

void MobileEngineBuilder::addWatchdog() {
  envoy::config::bootstrap::v3::Watchdogs watchdog;
  watchdog.mutable_main_thread_watchdog()->mutable_megamiss_timeout()->set_seconds(60);
  watchdog.mutable_main_thread_watchdog()->mutable_miss_timeout()->set_seconds(60);
  watchdog.mutable_worker_watchdog()->mutable_megamiss_timeout()->set_seconds(60);
  watchdog.mutable_worker_watchdog()->mutable_miss_timeout()->set_seconds(60);
  setWatchdog(std::move(watchdog));
}

absl::Status MobileEngineBuilder::configureNode(envoy::config::core::v3::Node* node) {
  node->set_id(node_id_.empty() ? "envoy-mobile" : node_id_);
  node->set_cluster("envoy-mobile");
  if (node_locality_ && !node_locality_->region.empty()) {
    node->mutable_locality()->set_region(node_locality_->region);
    node->mutable_locality()->set_zone(node_locality_->zone);
    node->mutable_locality()->set_sub_zone(node_locality_->sub_zone);
  }
  if (node_metadata_.has_value()) {
    *node->mutable_metadata() = *node_metadata_;
  }
  Protobuf::Struct& metadata = *node->mutable_metadata();
  (*metadata.mutable_fields())["app_id"].set_string_value(app_id_);
  (*metadata.mutable_fields())["app_version"].set_string_value(app_version_);
  (*metadata.mutable_fields())["device_os"].set_string_value(device_os_);

  return absl::OkStatus();
}

absl::Status MobileEngineBuilder::configXds(envoy::config::bootstrap::v3::Bootstrap* bootstrap) {
  (void)bootstrap;
#ifdef ENVOY_MOBILE_XDS
  if (xds_builder_) {
    xds_builder_->build(*bootstrap);
  }
#endif // ENVOY_MOBILE_XDS
  return absl::OkStatus();
}

EngineSharedPtr MobileEngineBuilder::build() {
  return Platform::EngineBuilderBase<MobileEngineBuilder>::build().value();
}

void MobileEngineBuilder::preRunSetup(InternalEngine* engine) {
  engine->disableDnsRefreshOnNetworkChange(disable_dns_refresh_on_network_change_);
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
    registerAppleProxyResolver(proxy_settings_refresh_interval_secs_);
  }
#endif
}

void MobileEngineBuilder::postRunSetup(Engine* engine) {
  if (enable_network_change_monitor_) {
    engine->initializeNetworkChangeMonitor();
  }
}

} // namespace Platform
} // namespace Envoy
