#include "engine_builder.h"

#include <sstream>

#include "envoy/config/metrics/v3/metrics_service.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case.pb.h"
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer.pb.h"

#include "source/common/common/assert.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "fmt/core.h"
#include "library/common/config/internal.h"
#include "library/common/engine.h"
#include "library/common/extensions/cert_validator/platform_bridge/platform_bridge.pb.h"
#include "library/common/extensions/filters/http/local_error/filter.pb.h"
#include "library/common/extensions/filters/http/network_configuration/filter.pb.h"
#include "library/common/extensions/filters/http/socket_tag/filter.pb.h"
#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

namespace {
// Inserts `filter_config` into the "custom_filters" target in `config_template`.
void insertCustomFilter(const std::string& filter_config, std::string& config_template) {
  absl::StrReplaceAll({{"#{custom_filters}", absl::StrCat("#{custom_filters}\n", filter_config)}},
                      &config_template);
}

bool generatedStringMatchesGeneratedBoostrap(
    std::string& config_str, std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap) {
  Thread::SkipAsserts skip;
  ProtobufMessage::StrictValidationVisitorImpl visitor;
  envoy::config::bootstrap::v3::Bootstrap config_bootstrap;
  MessageUtil::loadFromYaml(absl::StrCat(config_header, config_str), config_bootstrap, visitor);

  Protobuf::util::MessageDifferencer differencer;
  differencer.set_message_field_comparison(Protobuf::util::MessageDifferencer::EQUIVALENT);
  differencer.set_repeated_field_comparison(Protobuf::util::MessageDifferencer::AS_SET);

  bool same = differencer.Compare(config_bootstrap, *bootstrap);

  if (!same) {
    std::cerr << config_bootstrap.DebugString();
    std::cerr << "============================================";
    std::cerr << bootstrap->DebugString();
  }
  return same;
}

} // namespace

EngineBuilder::EngineBuilder(std::string config_template)
    : callbacks_(std::make_shared<EngineCallbacks>()), config_template_(config_template) {
  config_bootstrap_incompatible_ = true;
}

EngineBuilder::EngineBuilder() : EngineBuilder(std::string(config_template)) {
  // Using the default config template is bootstrap compatible.
  config_bootstrap_incompatible_ = false;
}

EngineBuilder& EngineBuilder::addLogLevel(LogLevel log_level) {
  log_level_ = log_level;
  return *this;
}

EngineBuilder& EngineBuilder::setOnEngineRunning(std::function<void()> closure) {
  callbacks_->on_engine_running = closure;
  return *this;
}

void EngineBuilder::bootstrapIncompatible() {
  ENVOY_BUG(!use_bootstrap_, "Function not compatible with bootstrap builder");
  config_bootstrap_incompatible_ = true;
}

EngineBuilder& EngineBuilder::addStatsSinks(const std::vector<std::string>& stat_sinks) {
  bootstrapIncompatible();
  stat_sinks_ = stat_sinks;
  return *this;
}

EngineBuilder& EngineBuilder::addGrpcStatsDomain(std::string stats_domain) {
  stats_domain_ = std::move(stats_domain);
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

EngineBuilder& EngineBuilder::addDnsPreresolveHostnames(std::string dns_preresolve_hostnames) {
  bootstrapIncompatible();
  dns_preresolve_hostnames_ = std::move(dns_preresolve_hostnames);
  return *this;
}

EngineBuilder& EngineBuilder::addMaxConnectionsPerHost(int max_connections_per_host) {
  max_connections_per_host_ = max_connections_per_host;
  return *this;
}

EngineBuilder& EngineBuilder::useDnsSystemResolver(bool use_system_resolver) {
  use_system_resolver_ = use_system_resolver;
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

EngineBuilder& EngineBuilder::addStatsFlushSeconds(int stats_flush_seconds) {
  stats_flush_seconds_ = stats_flush_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addVirtualClusters(std::string virtual_clusters) {
  bootstrapIncompatible();
  virtual_clusters_ = std::move(virtual_clusters);
  return *this;
}

EngineBuilder& EngineBuilder::addKeyValueStore(std::string name,
                                               KeyValueStoreSharedPtr key_value_store) {
  bootstrapIncompatible();
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

EngineBuilder& EngineBuilder::enableGzip(bool gzip_on) {
  gzip_filter_ = gzip_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableBrotli(bool brotli_on) {
  brotli_filter_ = brotli_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableSocketTagging(bool socket_tagging_on) {
  socket_tagging_filter_ = socket_tagging_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableAdminInterface(bool admin_interface_on) {
  bootstrapIncompatible();
  admin_interface_enabled_ = admin_interface_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableHappyEyeballs(bool happy_eyeballs_on) {
  enable_happy_eyeballs_ = happy_eyeballs_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableHttp3(bool http3_on) {
  enable_http3_ = http3_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableInterfaceBinding(bool interface_binding_on) {
  enable_interface_binding_ = interface_binding_on;
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

EngineBuilder&
EngineBuilder::enablePlatformCertificatesValidation(bool platform_certificates_validation_on) {
  platform_certificates_validation_on_ = platform_certificates_validation_on;
  return *this;
}

EngineBuilder& EngineBuilder::addStringAccessor(std::string name,
                                                StringAccessorSharedPtr accessor) {
  bootstrapIncompatible();
  string_accessors_[std::move(name)] = std::move(accessor);
  return *this;
}

EngineBuilder& EngineBuilder::addNativeFilter(std::string name, std::string typed_config) {
  bootstrapIncompatible();
  native_filter_chain_.emplace_back(std::move(name), std::move(typed_config));
  return *this;
}

EngineBuilder& EngineBuilder::addPlatformFilter(std::string name) {
  bootstrapIncompatible();
  platform_filters_.push_back(std::move(name));
  return *this;
}

EngineBuilder& EngineBuilder::setUseBootstrap() {
  ENVOY_BUG(!config_bootstrap_incompatible_, "Config not compatible with bootstrap builder");
  use_bootstrap_ = true;
  return *this;
}

std::string EngineBuilder::generateConfigStr() const {
  std::vector<std::pair<std::string, std::string>> replacements {
    {"connect_timeout", fmt::format("{}s", connect_timeout_seconds_)},
        {"dns_fail_base_interval", fmt::format("{}s", dns_failure_refresh_seconds_base_)},
        {"dns_fail_max_interval", fmt::format("{}s", dns_failure_refresh_seconds_max_)},
        {"dns_lookup_family", enable_happy_eyeballs_ ? "ALL" : "V4_PREFERRED"},
        {"dns_min_refresh_rate", fmt::format("{}s", dns_min_refresh_seconds_)},
        {"dns_multiple_addresses", enable_happy_eyeballs_ ? "true" : "false"},
        {"dns_preresolve_hostnames", dns_preresolve_hostnames_},
        {"dns_refresh_rate", fmt::format("{}s", dns_refresh_seconds_)},
        {"dns_query_timeout", fmt::format("{}s", dns_query_timeout_seconds_)},
        {"enable_drain_post_dns_refresh", enable_drain_post_dns_refresh_ ? "true" : "false"},
        {"enable_interface_binding", enable_interface_binding_ ? "true" : "false"},
        {"h2_connection_keepalive_idle_interval",
         fmt::format("{}s", h2_connection_keepalive_idle_interval_milliseconds_ / 1000.0)},
        {"h2_connection_keepalive_timeout",
         fmt::format("{}s", h2_connection_keepalive_timeout_seconds_)},
        {
            "metadata",
            fmt::format("{{ device_os: {}, app_version: {}, app_id: {} }}", device_os_,
                        app_version_, app_id_),
        },
        {"max_connections_per_host", fmt::format("{}", max_connections_per_host_)},
        {"stats_flush_interval", fmt::format("{}s", stats_flush_seconds_)},
        {"stream_idle_timeout", fmt::format("{}s", stream_idle_timeout_seconds_)},
        {"trust_chain_verification",
         enforce_trust_chain_verification_ ? "VERIFY_TRUST_CHAIN" : "ACCEPT_UNTRUSTED"},
        {"per_try_idle_timeout", fmt::format("{}s", per_try_idle_timeout_seconds_)},
        {"virtual_clusters", virtual_clusters_},
#if defined(__ANDROID_API__)
        {"force_ipv6", "true"},
#endif
  };
  if (!stats_domain_.empty()) {
    replacements.push_back({"stats_domain", stats_domain_});
  }

  // NOTE: this does not include support for custom filters
  // which are not yet supported in the C++ platform implementation
  std::ostringstream config_builder;
  config_builder << "!ignore platform_defs:" << std::endl;
  for (const auto& [key, value] : replacements) {
    config_builder << "- &" << key << " " << value << std::endl;
  }
  std::vector<std::string> stat_sinks = stat_sinks_;
  if (!stats_domain_.empty()) {
    stat_sinks.push_back("*base_metrics_service");
  }
  if (!stat_sinks.empty()) {
    config_builder << "- &stats_sinks [";
    config_builder << absl::StrJoin(stat_sinks, ",");
    config_builder << "] " << std::endl;
  }

  const std::string& cert_validation_template =
      (platform_certificates_validation_on_ ? platform_cert_validation_context_template
                                            : default_cert_validation_context_template);
  config_builder << cert_validation_template << std::endl;

  std::string config_template = config_template_;
  if (gzip_filter_) {
    insertCustomFilter(gzip_config_insert, config_template);
  }
  if (brotli_filter_) {
    insertCustomFilter(brotli_config_insert, config_template);
  }
  if (socket_tagging_filter_) {
    insertCustomFilter(socket_tag_config_insert, config_template);
  }
  if (enable_http3_) {
    insertCustomFilter(alternate_protocols_cache_filter_insert, config_template);
  }

  for (const NativeFilterConfig& filter : native_filter_chain_) {
    std::string filter_config = absl::StrReplaceAll(
        native_filter_template, {{"{{ native_filter_name }}", filter.name_},
                                 {"{{ native_filter_typed_config }}", filter.typed_config_}});
    insertCustomFilter(filter_config, config_template);
  }

  for (const std::string& name : platform_filters_) {
    std::string filter_config =
        absl::StrReplaceAll(platform_filter_template, {{"{{ platform_filter_name }}", name}});
    insertCustomFilter(filter_config, config_template);
  }

  config_builder << config_template;

  if (admin_interface_enabled_) {
    config_builder << "admin: *admin_interface" << std::endl;
  }

  auto config_str = config_builder.str();
  if (config_str.find("{{") != std::string::npos) {
    throw std::runtime_error("could not resolve all template keys in config");
  }

  if (!config_bootstrap_incompatible_) {
    // In debug mode, unless the config is known to be bootstrap incompatible,
    // check equivalence.
    ASSERT(generatedStringMatchesGeneratedBoostrap(config_str, generateBootstrap()));
  }

  return config_str;
}

std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> EngineBuilder::generateBootstrap() const {
  auto bootstrap = std::make_unique<envoy::config::bootstrap::v3::Bootstrap>();

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
  auto* remote_service = route_config->add_virtual_hosts();
  remote_service->set_name("remote_service");
  remote_service->add_domains("127.0.0.1");
  auto* route = remote_service->add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_direct_response()->set_status(404);
  route->mutable_direct_response()->mutable_body()->set_inline_string("not found");
  route->add_request_headers_to_remove("x-forwarded-proto");
  route->add_request_headers_to_remove("x-envoy-mobile-cluster");
  auto* api_service = route_config->add_virtual_hosts();
  api_service->set_name("api");
  api_service->set_include_attempt_count_in_response(true);
  api_service->add_domains("*");
  route = api_service->add_routes();
  route->mutable_match()->set_prefix("/");
  route->add_request_headers_to_remove("x-forwarded-proto");
  route->add_request_headers_to_remove("x-envoy-mobile-cluster");
  auto* route_to = route->mutable_route();
  route_to->set_cluster_header("x-envoy-mobile-cluster");
  route_to->mutable_timeout()->set_seconds(0);
  route_to->mutable_retry_policy()->mutable_per_try_idle_timeout()->set_seconds(
      per_try_idle_timeout_seconds_);
  auto* backoff = route_to->mutable_retry_policy()->mutable_retry_back_off();
  backoff->mutable_base_interval()->set_nanos(250000000);
  backoff->mutable_max_interval()->set_seconds(60);

  // Set up the optional filters
  if (enable_http3_) {
    envoy::extensions::filters::http::alternate_protocols_cache::v3::FilterConfig cache_config;
    cache_config.mutable_alternate_protocols_cache_options()->set_name(
        "default_alternate_protocols_cache");
    auto* cache_filter = hcm->add_http_filters();
    cache_filter->set_name("alternate_protocols_cache");
    cache_filter->mutable_typed_config()->PackFrom(cache_config);
  }

  if (socket_tagging_filter_) {
    envoymobile::extensions::filters::http::socket_tag::SocketTag tag_config;
    auto* tag_filter = hcm->add_http_filters();
    tag_filter->set_name("envoy.filters.http.socket_tag");
    tag_filter->mutable_typed_config()->PackFrom(tag_config);
  }
  if (brotli_filter_) {
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
  if (gzip_filter_) {
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
  if (enable_happy_eyeballs_) {
    dns_cache_config->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::ALL);
  } else {
    dns_cache_config->set_dns_lookup_family(envoy::config::cluster::v3::Cluster::V4_PREFERRED);
  }
  dns_cache_config->mutable_host_ttl()->set_seconds(86400);
  dns_cache_config->mutable_dns_min_refresh_rate()->set_seconds(dns_min_refresh_seconds_);
  dns_cache_config->mutable_dns_refresh_rate()->set_seconds(dns_refresh_seconds_);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_base_interval()->set_seconds(
      dns_failure_refresh_seconds_base_);
  dns_cache_config->mutable_dns_failure_refresh_rate()->mutable_max_interval()->set_seconds(
      dns_failure_refresh_seconds_max_);
  dns_cache_config->mutable_dns_query_timeout()->set_seconds(dns_query_timeout_seconds_);

#if defined(__APPLE__)
  envoy::extensions::network::dns_resolver::apple::v3::AppleDnsResolverConfig resolver_config;
  dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
      "envoy.network.dns_resolver.apple");
#else
  envoy::extensions::network::dns_resolver::getaddrinfo::v3::GetAddrInfoDnsResolverConfig
      resolver_config;
  dns_cache_config->mutable_typed_dns_resolver_config()->set_name(
      "envoy.network.dns_resolver.getaddrinfo");
#endif
  dns_cache_config->mutable_typed_dns_resolver_config()->mutable_typed_config()->PackFrom(
      resolver_config);

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
    validation->mutable_custom_validator_config()->set_name(
        "envoy_mobile.cert_validator.platform_bridge_cert_validator");
    validation->mutable_custom_validator_config()->mutable_typed_config()->PackFrom(validator);

  } else {
    const char* inline_certs = ""
#ifndef EXCLUDE_CERTIFICATES
#include "library/common/config/no_whitespace_certificates.inc"
#endif
                               "";
    validation->mutable_trusted_ca()->set_inline_string(inline_certs);
  }
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
      ssl_proxy_socket;
  ssl_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.tls");
  ssl_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(tls_socket);

  envoy::config::core::v3::TransportSocket base_tls_socket;
  base_tls_socket.set_name("envoy.transport_sockets.http_11_proxy");
  base_tls_socket.mutable_typed_config()->PackFrom(ssl_proxy_socket);

  // Stats cluster
  auto* stats_cluster = static_resources->add_clusters();
  stats_cluster->set_name("stats");
  stats_cluster->set_type(envoy::config::cluster::v3::Cluster::LOGICAL_DNS);
  stats_cluster->mutable_connect_timeout()->set_seconds(connect_timeout_seconds_);
  stats_cluster->mutable_dns_refresh_rate()->set_seconds(dns_refresh_seconds_);
  stats_cluster->mutable_transport_socket()->CopyFrom(base_tls_socket);
  stats_cluster->mutable_load_assignment()->set_cluster_name("stats");
  auto* address = stats_cluster->mutable_load_assignment()
                      ->add_endpoints()
                      ->add_lb_endpoints()
                      ->mutable_endpoint()
                      ->mutable_address();
  if (stats_domain_.empty()) {
    address->mutable_socket_address()->set_address("127.0.0.1");
  } else {
    address->mutable_socket_address()->set_address(stats_domain_);
  }
  address->mutable_socket_address()->set_port_value(443);
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions h2_protocol_options;
  h2_protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options();
  (*stats_cluster->mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(h2_protocol_options);
  stats_cluster->mutable_wait_for_warm_on_init();

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

  // Base cleartext cluster set up
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

  // Base cleartext cluster.
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

  // Base h2 cluster.
  auto* base_h2 = static_resources->add_clusters();
  base_h2->set_name("base_h2");
  h2_protocol_options.mutable_explicit_http_config()->mutable_http2_protocol_options()->CopyFrom(
      *alpn_options.mutable_auto_config()->mutable_http2_protocol_options());
  h2_protocol_options.mutable_upstream_http_protocol_options()->set_auto_sni(true);
  h2_protocol_options.mutable_upstream_http_protocol_options()->set_auto_san_validation(true);
  base_h2->mutable_connect_timeout()->set_seconds(connect_timeout_seconds_);
  base_h2->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  base_h2->mutable_cluster_type()->CopyFrom(base_cluster_type);
  base_h2->mutable_transport_socket()->set_name("envoy.transport_sockets.http_11_proxy");
  base_h2->mutable_transport_socket()->mutable_typed_config()->PackFrom(ssl_proxy_socket);
  base_h2->mutable_upstream_connection_options()->CopyFrom(
      *base_cluster->mutable_upstream_connection_options());
  base_h2->mutable_circuit_breakers()->CopyFrom(*base_cluster->mutable_circuit_breakers());
  (*base_h2->mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(h2_protocol_options);

  // Base h3 cluster set up
  envoy::extensions::transport_sockets::quic::v3::QuicUpstreamTransport h3_inner_socket;
  tls_socket.mutable_common_tls_context()->mutable_alpn_protocols()->Clear();
  h3_inner_socket.mutable_upstream_tls_context()->CopyFrom(tls_socket);
  envoy::extensions::transport_sockets::http_11_proxy::v3::Http11ProxyUpstreamTransport
      h3_proxy_socket;
  h3_proxy_socket.mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_inner_socket);
  h3_proxy_socket.mutable_transport_socket()->set_name("envoy.transport_sockets.quic");
  alpn_options.mutable_auto_config()->mutable_http3_protocol_options();
  alpn_options.mutable_auto_config()->mutable_alternate_protocols_cache_options()->set_name(
      "default_alternate_protocols_cache");

  // Base h3 cluster
  auto* base_h3 = static_resources->add_clusters();
  base_h3->set_name("base_h3");
  base_h3->mutable_connect_timeout()->set_seconds(connect_timeout_seconds_);
  base_h3->set_lb_policy(envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED);
  base_h3->mutable_cluster_type()->CopyFrom(base_cluster_type);
  base_h3->mutable_transport_socket()->set_name("envoy.transport_sockets.http_11_proxy");
  base_h3->mutable_transport_socket()->mutable_typed_config()->PackFrom(h3_proxy_socket);
  base_h3->mutable_upstream_connection_options()->CopyFrom(
      *base_cluster->mutable_upstream_connection_options());
  base_h3->mutable_circuit_breakers()->CopyFrom(*base_cluster->mutable_circuit_breakers());
  (*base_h3->mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(alpn_options);

  // Set up stats.
  bootstrap->mutable_stats_flush_interval()->set_seconds(stats_flush_seconds_);
  bootstrap->mutable_stats_sinks();
  auto* list = bootstrap->mutable_stats_config()->mutable_stats_matcher()->mutable_inclusion_list();
  list->add_patterns()->set_prefix("cluster.base.upstream_rq_");
  list->add_patterns()->set_prefix("cluster.base_h2.upstream_rq_");
  list->add_patterns()->set_prefix("cluster.stats.upstream_rq_");
  list->add_patterns()->set_prefix("cluster.base.upstream_cx_");
  list->add_patterns()->set_prefix("cluster.base_h2.upstream_cx_");
  list->add_patterns()->set_prefix("cluster.stats.upstream_cx_");
  list->add_patterns()->set_exact("cluster.base.http2.keepalive_timeout");
  list->add_patterns()->set_exact("cluster.base_h2.http2.keepalive_timeout");
  list->add_patterns()->set_exact("cluster.stats.http2.keepalive_timeout");
  list->add_patterns()->set_prefix("http.hcm.downstream_rq_");
  list->add_patterns()->set_prefix("http.hcm.decompressor.");
  list->add_patterns()->set_prefix("pulse.");
  list->add_patterns()->mutable_safe_regex()->set_regex(
      "^vhost\\.[\\w]+\\.vcluster\\.[\\w]+?\\.upstream_rq_(?:[12345]xx|[3-5][0-9][0-9]|retry|"
      "total)");
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
  ProtobufWkt::Struct& flags =
      *(*runtime_values.mutable_fields())["reloadable_features"].mutable_struct_value();
  (*flags.mutable_fields())["allow_multiple_dns_addresses"].set_bool_value(enable_happy_eyeballs_);
  (*flags.mutable_fields())["always_use_v6"].set_bool_value(false);
  (*flags.mutable_fields())["skip_dns_lookup_for_proxied_requests"].set_bool_value(false);
  (*runtime_values.mutable_fields())["disallow_global_stats"].set_bool_value("true");
  ProtobufWkt::Struct& overload_values =
      *(*envoy_layer.mutable_fields())["overload"].mutable_struct_value();
  (*overload_values.mutable_fields())["global_downstream_max_connections"].set_string_value(
      "4294967295");
  runtime->mutable_static_layer()->MergeFrom(envoy_layer);

  bootstrap->mutable_typed_dns_resolver_config()->CopyFrom(
      *dns_cache_config->mutable_typed_dns_resolver_config());

  if (!stats_domain_.empty()) {
    envoy::config::metrics::v3::MetricsServiceConfig metrics_config;
    metrics_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("stats");
    metrics_config.mutable_report_counters_as_deltas()->set_value(true);
    metrics_config.set_transport_api_version(envoy::config::core::v3::ApiVersion::V3);
    metrics_config.set_emit_tags_as_labels(true);
    auto* sink = bootstrap->add_stats_sinks();
    sink->set_name("envoy.metrics_service");
    sink->mutable_typed_config()->PackFrom(metrics_config);
  }

  return bootstrap;
}

EngineSharedPtr EngineBuilder::build() {
  envoy_logger null_logger;
  null_logger.log = nullptr;
  null_logger.release = envoy_noop_const_release;
  null_logger.context = nullptr;

  envoy_event_tracker null_tracker{};

  std::string config_str;
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> bootstrap;
  if (use_bootstrap_) {
    bootstrap = generateBootstrap();
  } else {
    if (config_override_for_tests_.empty()) {
      config_str = generateConfigStr();
    } else {
      config_str = config_override_for_tests_;
    }
  }
  envoy_engine_t envoy_engine =
      init_engine(callbacks_->asEnvoyEngineCallbacks(), null_logger, null_tracker);

  for (const auto& [name, store] : key_value_stores_) {
    // TODO(goaway): This leaks, but it's tied to the life of the engine.
    auto* api = new envoy_kv_store();
    *api = store->asEnvoyKeyValueStore();
    register_platform_api(name.c_str(), api);
  }

  for (const auto& [name, accessor] : string_accessors_) {
    // TODO(RyanTheOptimist): This leaks, but it's tied to the life of the engine.
    auto* api = new envoy_string_accessor();
    *api = StringAccessor::asEnvoyStringAccessor(accessor);
    register_platform_api(name.c_str(), api);
  }

  Engine* engine = new Engine(envoy_engine);

  if (auto cast_engine = reinterpret_cast<Envoy::Engine*>(envoy_engine)) {
    auto options = std::make_unique<Envoy::OptionsImpl>();
    if (bootstrap) {
      options->setConfigProto(std::move(bootstrap));
    } else {
      options->setConfigYaml(absl::StrCat(config_header, config_str));
    }
    options->setLogLevel(options->parseAndValidateLogLevel(logLevelToString(log_level_).c_str()));
    options->setConcurrency(1);
    if (!admin_address_path_for_tests_.empty()) {
      options->setAdminAddressPath(admin_address_path_for_tests_);
    }
    cast_engine->run(std::move(options));
  }

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy
