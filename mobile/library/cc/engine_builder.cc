#include "engine_builder.h"

#include <sstream>

#include "absl/strings/str_replace.h"
#include "fmt/core.h"
#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

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

std::string EngineBuilder::generateConfigStr() {
#if defined(__APPLE__)
  std::string dns_resolver_name = "envoy.network.dns_resolver.apple";
  std::string dns_resolver_config =
      "{\"@type\":\"type.googleapis.com/"
      "envoy.extensions.network.dns_resolver.apple.v3.AppleDnsResolverConfig\"}";
#else
  std::string dns_resolver_name = "";
  std::string dns_resolver_config = "";
  if (this->use_system_resolver_) {
    dns_resolver_name = "envoy.network.dns_resolver.getaddrinfo";
    dns_resolver_config =
        "{\"@type\":\"type.googleapis.com/"
        "envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig\"}";
  } else {
    dns_resolver_name = "envoy.network.dns_resolver.cares";
    dns_resolver_config =
        "{\"@type\":\"type.googleapis.com/"
        "envoy.extensions.network.dns_resolver.cares.v3.CaresDnsResolverConfig\"}";
  }
#endif

  std::vector<std::pair<std::string, std::string>> replacements{
      {"connect_timeout", fmt::format("{}s", this->connect_timeout_seconds_)},
      {"dns_fail_base_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_base_)},
      {"dns_fail_max_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_max_)},
      {"dns_preresolve_hostnames", this->dns_preresolve_hostnames_},
      {"dns_refresh_rate", fmt::format("{}s", this->dns_refresh_seconds_)},
      {"dns_query_timeout", fmt::format("{}s", this->dns_query_timeout_seconds_)},
      {"dns_resolver_name", dns_resolver_name},
      {"dns_resolver_config", dns_resolver_config},
      {"h2_connection_keepalive_idle_interval",
       fmt::format("{}s", this->h2_connection_keepalive_idle_interval_milliseconds_ / 1000.0)},
      {"h2_connection_keepalive_timeout",
       fmt::format("{}s", this->h2_connection_keepalive_timeout_seconds_)},
      {
          "metadata",
          fmt::format("{{ device_os: {}, app_version: {}, app_id: {} }}", this->device_os_,
                      this->app_version_, this->app_id_),
      },
      {"stats_domain", this->stats_domain_},
      {"stats_flush_interval", fmt::format("{}s", this->stats_flush_seconds_)},
      {"stream_idle_timeout", fmt::format("{}s", this->stream_idle_timeout_seconds_)},
      {"per_try_idle_timeout", fmt::format("{}s", this->per_try_idle_timeout_seconds_)},
      {"virtual_clusters", this->virtual_clusters_},
  };

  // NOTE: this does not include support for custom filters
  // which are not yet supported in the C++ platform implementation
  std::ostringstream config_builder;
  config_builder << "!ignore platform_defs:" << std::endl;
  for (const auto& [key, value] : replacements) {
    config_builder << "- &" << key << " " << value << std::endl;
  }
  if (this->gzip_filter_) {
    absl::StrReplaceAll(
        {{"#{custom_filters}", absl::StrCat("#{custom_filters}\n", gzip_config_insert)}},
        &config_template_);
  }
  if (this->brotli_filter_) {
    absl::StrReplaceAll(
        {{"#{custom_filters}", absl::StrCat("#{custom_filters}\n", brotli_config_insert)}},
        &config_template_);
  }

  if (this->socket_tagging_filter_) {
    absl::StrReplaceAll(
        {{"#{custom_filters}", absl::StrCat("#{custom_filters}\n", socket_tag_config_insert)}},
        &config_template_);
  }

  config_builder << config_template_;

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
  auto envoy_engine =
      init_engine(this->callbacks_->asEnvoyEngineCallbacks(), null_logger, null_tracker);

  for (auto it = key_value_stores_.begin(); it != key_value_stores_.end(); ++it) {
    // TODO(goaway): This leaks, but it's tied to the life of the engine.
    envoy_kv_store* api = static_cast<envoy_kv_store*>(safe_malloc(sizeof(envoy_kv_store)));
    *api = it->second->asEnvoyKeyValueStore();
    register_platform_api(it->first.c_str(), api);
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
