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
  this->log_level_ = log_level;
  return *this;
}

EngineBuilder& EngineBuilder::setOnEngineRunning(std::function<void()> closure) {
  this->callbacks_->on_engine_running = closure;
  return *this;
}

EngineBuilder& EngineBuilder::addStatsSinks(const std::vector<std::string>& stat_sinks) {
  this->stat_sinks_ = stat_sinks;
  return *this;
}

EngineBuilder& EngineBuilder::addGrpcStatsDomain(const std::string& stats_domain) {
  this->stats_domain_ = stats_domain;
  return *this;
}

EngineBuilder& EngineBuilder::addConnectTimeoutSeconds(int connect_timeout_seconds) {
  this->connect_timeout_seconds_ = connect_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsRefreshSeconds(int dns_refresh_seconds) {
  this->dns_refresh_seconds_ = dns_refresh_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsMinRefreshSeconds(int dns_min_refresh_seconds) {
  this->dns_min_refresh_seconds_ = dns_min_refresh_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsFailureRefreshSeconds(int base, int max) {
  this->dns_failure_refresh_seconds_base_ = base;
  this->dns_failure_refresh_seconds_max_ = max;
  return *this;
}

EngineBuilder& EngineBuilder::addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds) {
  this->dns_query_timeout_seconds_ = dns_query_timeout_seconds;
  return *this;
}

EngineBuilder&
EngineBuilder::addDnsPreresolveHostnames(const std::string& dns_preresolve_hostnames) {
  this->dns_preresolve_hostnames_ = dns_preresolve_hostnames;
  return *this;
}

EngineBuilder& EngineBuilder::addMaxConnectionsPerHost(int max_connections_per_host) {
  this->max_connections_per_host_ = max_connections_per_host;
  return *this;
}

EngineBuilder& EngineBuilder::useDnsSystemResolver(bool use_system_resolver) {
  this->use_system_resolver_ = use_system_resolver;
  return *this;
}

EngineBuilder& EngineBuilder::addH2ConnectionKeepaliveIdleIntervalMilliseconds(
    int h2_connection_keepalive_idle_interval_milliseconds) {
  this->h2_connection_keepalive_idle_interval_milliseconds_ =
      h2_connection_keepalive_idle_interval_milliseconds;
  return *this;
}

EngineBuilder&
EngineBuilder::addH2ConnectionKeepaliveTimeoutSeconds(int h2_connection_keepalive_timeout_seconds) {
  this->h2_connection_keepalive_timeout_seconds_ = h2_connection_keepalive_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addStatsFlushSeconds(int stats_flush_seconds) {
  this->stats_flush_seconds_ = stats_flush_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::addVirtualClusters(const std::string& virtual_clusters) {
  this->virtual_clusters_ = virtual_clusters;
  return *this;
}

EngineBuilder& EngineBuilder::addKeyValueStore(const std::string& name,
                                               KeyValueStoreSharedPtr key_value_store) {
  this->key_value_stores_[name] = key_value_store;
  return *this;
}

EngineBuilder& EngineBuilder::setAppVersion(const std::string& app_version) {
  this->app_version_ = app_version;
  return *this;
}

EngineBuilder& EngineBuilder::setAppId(const std::string& app_id) {
  this->app_id_ = app_id;
  return *this;
}

EngineBuilder& EngineBuilder::setDeviceOs(const std::string& device_os) {
  this->device_os_ = device_os;
  return *this;
}

EngineBuilder& EngineBuilder::setStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds) {
  this->stream_idle_timeout_seconds_ = stream_idle_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds) {
  this->per_try_idle_timeout_seconds_ = per_try_idle_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::enableGzip(bool gzip_on) {
  this->gzip_filter_ = gzip_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableBrotli(bool brotli_on) {
  this->brotli_filter_ = brotli_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableSocketTagging(bool socket_tagging_on) {
  this->socket_tagging_filter_ = socket_tagging_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableAdminInterface(bool admin_interface_on) {
  this->admin_interface_enabled_ = admin_interface_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableHappyEyeballs(bool happy_eyeballs_on) {
  this->enable_happy_eyeballs_ = happy_eyeballs_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableHttp3(bool http3_on) {
  this->enable_http3_ = http3_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableInterfaceBinding(bool interface_binding_on) {
  this->enable_interface_binding_ = interface_binding_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on) {
  this->enable_drain_post_dns_refresh_ = drain_post_dns_refresh_on;
  return *this;
}

EngineBuilder& EngineBuilder::enforceTrustChainVerification(bool trust_chain_verification_on) {
  this->enforce_trust_chain_verification_ = trust_chain_verification_on;
  return *this;
}

EngineBuilder& EngineBuilder::enableH2ExtendKeepaliveTimeout(bool h2_extend_keepalive_timeout_on) {
  this->h2_extend_keepalive_timeout_ = h2_extend_keepalive_timeout_on;
  return *this;
}

EngineBuilder&
EngineBuilder::enablePlatformCertificatesValidation(bool platform_certificates_validation_on) {
#if defined(__APPLE__)
  if (platform_certificates_validation_on) {
    PANIC("Certificates validation using platform provided APIs is not supported in IOS.");
  }
#endif
  this->platform_certificates_validation_on_ = platform_certificates_validation_on;
  return *this;
}

EngineBuilder& EngineBuilder::addStringAccessor(const std::string& name,
                                                StringAccessorSharedPtr accessor) {
  string_accessors_[name] = accessor;
  return *this;
}

EngineBuilder& EngineBuilder::addNativeFilter(const std::string& name,
                                              const std::string& typed_config) {
  native_filter_chain_.emplace_back(name, typed_config);
  return *this;
}

EngineBuilder& EngineBuilder::addPlatformFilter(const std::string& name) {
  platform_filters_.push_back(name);
  return *this;
}

std::string EngineBuilder::generateConfigStr() const {
  std::vector<std::pair<std::string, std::string>> replacements {
    {"connect_timeout", fmt::format("{}s", this->connect_timeout_seconds_)},
        {"dns_fail_base_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_base_)},
        {"dns_fail_max_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_max_)},
        {"dns_lookup_family", enable_happy_eyeballs_ ? "ALL" : "V4_PREFERRED"},
        {"dns_min_refresh_rate", fmt::format("{}s", this->dns_min_refresh_seconds_)},
        {"dns_multiple_addresses", enable_happy_eyeballs_ ? "true" : "false"},
        {"dns_preresolve_hostnames", this->dns_preresolve_hostnames_},
        {"dns_refresh_rate", fmt::format("{}s", this->dns_refresh_seconds_)},
        {"dns_query_timeout", fmt::format("{}s", this->dns_query_timeout_seconds_)},
        {"enable_drain_post_dns_refresh", enable_drain_post_dns_refresh_ ? "true" : "false"},
        {"enable_interface_binding", enable_interface_binding_ ? "true" : "false"},
        {"h2_connection_keepalive_idle_interval",
         fmt::format("{}s", this->h2_connection_keepalive_idle_interval_milliseconds_ / 1000.0)},
        {"h2_connection_keepalive_timeout",
         fmt::format("{}s", this->h2_connection_keepalive_timeout_seconds_)},
        {"h2_delay_keepalive_timeout", h2_extend_keepalive_timeout_ ? "true" : "false"},
        {
            "metadata",
            fmt::format("{{ device_os: {}, app_version: {}, app_id: {} }}", this->device_os_,
                        this->app_version_, this->app_id_),
        },
        {"max_connections_per_host", fmt::format("{}", this->max_connections_per_host_)},
        {"stats_domain", this->stats_domain_},
        {"stats_flush_interval", fmt::format("{}s", this->stats_flush_seconds_)},
        {"stream_idle_timeout", fmt::format("{}s", this->stream_idle_timeout_seconds_)},
        {"trust_chain_verification",
         enforce_trust_chain_verification_ ? "VERIFY_TRUST_CHAIN" : "ACCEPT_UNTRUSTED"},
        {"per_try_idle_timeout", fmt::format("{}s", this->per_try_idle_timeout_seconds_)},
        {"virtual_clusters", this->virtual_clusters_},
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
      (this->platform_certificates_validation_on_ ? platform_cert_validation_context_template
                                                  : default_cert_validation_context_template);
  config_builder << cert_validation_template << std::endl;

  std::string config_template = config_template_;
  if (this->gzip_filter_) {
    insertCustomFilter(gzip_config_insert, config_template);
  }
  if (this->brotli_filter_) {
    insertCustomFilter(brotli_config_insert, config_template);
  }
  if (this->socket_tagging_filter_) {
    insertCustomFilter(socket_tag_config_insert, config_template);
  }
  if (this->enable_http3_) {
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

EngineSharedPtr EngineBuilder::build() {
  envoy_logger null_logger;
  null_logger.log = nullptr;
  null_logger.release = envoy_noop_const_release;
  null_logger.context = nullptr;

  envoy_event_tracker null_tracker{};

  std::string config_str;
  if (config_override_for_tests_.empty()) {
    config_str = this->generateConfigStr();
  } else {
    config_str = config_override_for_tests_;
  }
  envoy_engine_t envoy_engine =
      init_engine(this->callbacks_->asEnvoyEngineCallbacks(), null_logger, null_tracker);

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

  run_engine(envoy_engine, config_str.c_str(), logLevelToString(this->log_level_).c_str(),
             this->admin_address_path_for_tests_.c_str());

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  Engine* engine = new Engine(envoy_engine);
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy
