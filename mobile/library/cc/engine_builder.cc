#include "engine_builder.h"

#include <sstream>

#include "source/common/common/assert.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "fmt/core.h"
#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

namespace {
// Inserts `filter_config` into the "custom_filters" target in `config_template`.
void insertCustomFilter(const std::string& filter_config, std::string& config_template) {
  absl::StrReplaceAll({{"#{custom_filters}", absl::StrCat("#{custom_filters}\n", filter_config)}},
                      &config_template);
}
} // namespace

EngineBuilder::EngineBuilder(std::string config_template)
    : callbacks_(std::make_shared<EngineCallbacks>()), config_template_(config_template) {}
EngineBuilder::EngineBuilder() : EngineBuilder(std::string(config_template)) {}

EngineBuilder& EngineBuilder::addLogLevel(LogLevel log_level) {
  log_level_ = log_level;
  return *this;
}

EngineBuilder& EngineBuilder::setOnEngineRunning(std::function<void()> closure) {
  callbacks_->on_engine_running = closure;
  return *this;
}

EngineBuilder& EngineBuilder::addStatsSinks(const std::vector<std::string>& stat_sinks) {
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
  virtual_clusters_ = std::move(virtual_clusters);
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

EngineBuilder& EngineBuilder::enableH2ExtendKeepaliveTimeout(bool h2_extend_keepalive_timeout_on) {
  h2_extend_keepalive_timeout_ = h2_extend_keepalive_timeout_on;
  return *this;
}

EngineBuilder&
EngineBuilder::enablePlatformCertificatesValidation(bool platform_certificates_validation_on) {
#if defined(__APPLE__)
  if (platform_certificates_validation_on) {
    PANIC("Certificates validation using platform provided APIs is not supported in IOS.");
  }
#endif
  platform_certificates_validation_on_ = platform_certificates_validation_on;
  return *this;
}

EngineBuilder& EngineBuilder::addStringAccessor(std::string name,
                                                StringAccessorSharedPtr accessor) {
  string_accessors_[std::move(name)] = std::move(accessor);
  return *this;
}

EngineBuilder& EngineBuilder::addNativeFilter(std::string name, std::string typed_config) {
  native_filter_chain_.emplace_back(std::move(name), std::move(typed_config));
  return *this;
}

EngineBuilder& EngineBuilder::addPlatformFilter(std::string name) {
  platform_filters_.push_back(std::move(name));
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
        {"h2_delay_keepalive_timeout", h2_extend_keepalive_timeout_ ? "true" : "false"},
        {
            "metadata",
            fmt::format("{{ device_os: {}, app_version: {}, app_id: {} }}", device_os_,
                        app_version_, app_id_),
        },
        {"max_connections_per_host", fmt::format("{}", max_connections_per_host_)},
        {"stats_domain", stats_domain_},
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
  return config_str;
}

EngineSharedPtr EngineBuilder::build(bool create_logger) {
  envoy_logger null_logger;
  null_logger.log = nullptr;
  null_logger.release = envoy_noop_const_release;
  null_logger.context = nullptr;

  envoy_event_tracker null_tracker{};

  std::string config_str;
  if (config_override_for_tests_.empty()) {
    config_str = generateConfigStr();
  } else {
    config_str = config_override_for_tests_;
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

  run_engine(envoy_engine, config_str.c_str(), logLevelToString(log_level_).c_str(),
             admin_address_path_for_tests_.c_str(), create_logger);

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  Engine* engine = new Engine(envoy_engine);
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy
