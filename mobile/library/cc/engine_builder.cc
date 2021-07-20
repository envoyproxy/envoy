#include "engine_builder.h"

#include <sstream>

#include "fmt/core.h"
#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

EngineBuilder::EngineBuilder(std::string config_template) : config_template_(config_template) {}
EngineBuilder::EngineBuilder() : EngineBuilder(std::string(config_template)) {}

EngineBuilder& EngineBuilder::addLogLevel(LogLevel log_level) {
  this->log_level_ = log_level;
  this->callbacks_ = std::make_shared<EngineCallbacks>();
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

EngineBuilder&
EngineBuilder::addDnsPreresolveHostnames(const std::string& dns_preresolve_hostnames) {
  this->dns_preresolve_hostnames_ = dns_preresolve_hostnames;
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

std::string EngineBuilder::generateConfigStr() {
  std::vector<std::pair<std::string, std::string>> replacements{
      {"connect_timeout", fmt::format("{}s", this->connect_timeout_seconds_)},
      {"dns_fail_base_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_base_)},
      {"dns_fail_max_interval", fmt::format("{}s", this->dns_failure_refresh_seconds_max_)},
      {"dns_preresolve_hostnames", this->dns_preresolve_hostnames_},
      {"dns_refresh_rate", fmt::format("{}s", this->dns_refresh_seconds_)},
      {
          "metadata",
          fmt::format("{{ device_os: {}, app_version: {}, app_id: {} }}", this->device_os_,
                      this->app_version_, this->app_id_),
      },
      {"stats_domain", this->stats_domain_},
      {"stats_flush_interval", fmt::format("{}s", this->stats_flush_seconds_)},
      {"stream_idle_timeout", fmt::format("{}s", this->stream_idle_timeout_seconds_)},
      {"virtual_clusters", this->virtual_clusters_},
  };

  // NOTE: this does not include support for custom filters
  // which are not yet supported in the C++ platform implementation
  std::ostringstream config_builder;
  config_builder << "!ignore platform_defs:" << std::endl;
  for (const auto& [key, value] : replacements) {
    config_builder << "- &" << key << " " << value << std::endl;
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

  auto config_str = this->generateConfigStr();
  auto envoy_engine = init_engine(this->callbacks_->asEnvoyEngineCallbacks(), null_logger);
  run_engine(envoy_engine, config_str.c_str(), logLevelToString(this->log_level_).c_str());

  // we can't construct via std::make_shared
  // because Engine is only constructible as a friend
  Engine* engine = new Engine(envoy_engine);
  auto engine_ptr = EngineSharedPtr(engine);
  return engine_ptr;
}

} // namespace Platform
} // namespace Envoy
