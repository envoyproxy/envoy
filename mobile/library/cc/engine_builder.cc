#include "engine_builder.h"

#include "library/common/main_interface.h"

namespace Envoy {
namespace Platform {

namespace {

void c_on_engine_running(void* context) {
  EngineCallbacks* engine_callbacks = static_cast<EngineCallbacks*>(context);
  engine_callbacks->on_engine_running();
}

void c_on_exit(void* context) {
  // NOTE: this function is intentionally empty
  // as we don't actually do any post-processing on exit.
  (void)context;
}

} // namespace

EngineBuilder::EngineBuilder(std::string config_template) : config_template_(config_template) {}
EngineBuilder::EngineBuilder() : EngineBuilder(std::string(config_template)) {}

EngineBuilder& EngineBuilder::add_log_level(LogLevel log_level) {
  this->log_level_ = log_level;
  this->callbacks_ = std::make_shared<EngineCallbacks>();
  return *this;
}

EngineBuilder& EngineBuilder::set_on_engine_running(std::function<void()> closure) {
  this->callbacks_->on_engine_running = closure;
  return *this;
}

EngineBuilder& EngineBuilder::add_stats_domain(const std::string& stats_domain) {
  this->stats_domain_ = stats_domain;
  return *this;
}

EngineBuilder& EngineBuilder::add_connect_timeout_seconds(int connect_timeout_seconds) {
  this->connect_timeout_seconds_ = connect_timeout_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::add_dns_refresh_seconds(int dns_refresh_seconds) {
  this->dns_refresh_seconds_ = dns_refresh_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::add_dns_failure_refresh_seconds(int base, int max) {
  this->dns_failure_refresh_seconds_base_ = base;
  this->dns_failure_refresh_seconds_max_ = max;
  return *this;
}

EngineBuilder& EngineBuilder::add_stats_flush_seconds(int stats_flush_seconds) {
  this->stats_flush_seconds_ = stats_flush_seconds;
  return *this;
}

EngineBuilder& EngineBuilder::set_app_version(const std::string& app_version) {
  this->app_version_ = app_version;
  return *this;
}

EngineBuilder& EngineBuilder::set_app_id(const std::string& app_id) {
  this->app_id_ = app_id;
  return *this;
}

EngineBuilder& EngineBuilder::add_virtual_clusters(const std::string& virtual_clusters) {
  this->virtual_clusters_ = virtual_clusters;
  return *this;
}

EngineSharedPtr EngineBuilder::build() {
  std::vector<std::pair<std::string, std::string>> replacements{
      {"{{ app_id }}", this->app_id_},
      {"{{ app_version }}", this->app_version_},
      {"{{ connect_timeout_seconds }}", std::to_string(this->connect_timeout_seconds_)},
      {"{{ device_os }}", "python"},
      {"{{ dns_failure_refresh_rate_seconds_base }}",
       std::to_string(this->dns_failure_refresh_seconds_base_)},
      {"{{ dns_failure_refresh_rate_seconds_max }}",
       std::to_string(this->dns_failure_refresh_seconds_max_)},
      {"{{ dns_refresh_rate_seconds }}", std::to_string(this->dns_refresh_seconds_)},
      {"{{ native_filter_chain }}", ""},
      {"{{ platform_filter_chain }}", ""},
      {"{{ stats_domain }}", this->stats_domain_},
      {"{{ stats_flush_interval_seconds }}", std::to_string(this->stats_flush_seconds_)},
      {"{{ virtual_clusters }}", this->virtual_clusters_},
  };

  std::string config_str = this->config_template_;
  for (const auto& pair : replacements) {
    const auto& key = pair.first;
    const auto& value = pair.second;

    size_t idx = 0;
    while ((idx = config_str.find(key, idx)) != std::string::npos) {
      config_str.replace(idx, key.size(), value);
    }
  }

  envoy_logger null_logger{
      .log = nullptr,
      .release = envoy_noop_const_release,
      .context = nullptr,
  };

  envoy_engine_callbacks envoy_callbacks{
      .on_engine_running = &c_on_engine_running,
      .on_exit = &c_on_exit,
      .context = this->callbacks_.get(),
  };

  Engine* engine =
      new Engine(init_engine(envoy_callbacks, null_logger), config_str, this->log_level_);
  return EngineSharedPtr(engine);
}

} // namespace Platform
} // namespace Envoy
