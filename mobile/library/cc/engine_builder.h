#pragma once

#include <memory>
#include <string>

#include "engine.h"
#include "log_level.h"

namespace Envoy {
namespace Platform {

class EngineBuilder {
public:
  EngineBuilder();

  EngineBuilder& add_log_level(LogLevel log_level);
  EngineBuilder& set_on_engine_running(std::function<void()> closure);

  EngineBuilder& add_stats_domain(const std::string& stats_domain);
  EngineBuilder& add_connect_timeout_seconds(int connect_timeout_seconds);
  EngineBuilder& add_dns_refresh_seconds(int dns_refresh_seconds);
  EngineBuilder& add_dns_failure_refresh_seconds(int base, int max);
  EngineBuilder& add_stats_flush_seconds(int stats_flush_seconds);
  EngineBuilder& set_app_version(const std::string& app_version);
  EngineBuilder& set_app_id(const std::string& app_id);
  EngineBuilder& add_virtual_clusters(const std::string& virtual_clusters);

  EngineSharedPtr build();

  // TODO(crockeo): add after filter integration
  // EngineBuilder& addPlatformFilter(name: String = UUID.randomUUID().toString(), factory: () ->
  // Filter): EngineBuilder& addNativeFilter(name: String = UUID.randomUUID().toString(),
  // typedConfig: String): EngineBuilder& addStringAccessor(name: String, accessor:
  // EnvoyStringAccessor): EngineBuilder {

private:
  LogLevel log_level_ = LogLevel::info;
  std::function<void()> on_engine_running_;

  std::string stats_domain_ = "0.0.0.0";
  int connect_timeout_seconds_ = 30;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int stats_flush_seconds_ = 60;
  std::string app_version_ = "unspecified";
  std::string app_id_ = "unspecified";
  std::string virtual_clusters_ = "[]";

  // TODO(crockeo): add after filter integration
  // private var platformFilterChain = mutableListOf<EnvoyHTTPFilterFactory>()
  // private var nativeFilterChain = mutableListOf<EnvoyNativeFilterConfig>()
  // private var stringAccessors = mutableMapOf<String, EnvoyStringAccessor>()
};

using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;

} // namespace Platform
} // namespace Envoy
