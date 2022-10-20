#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "engine.h"
#include "engine_callbacks.h"
#include "key_value_store.h"
#include "log_level.h"
#include "string_accessor.h"

namespace Envoy {
namespace Platform {

class EngineBuilder {
public:
  EngineBuilder(std::string config_template);
  EngineBuilder();

  EngineBuilder& addLogLevel(LogLevel log_level);
  EngineBuilder& setOnEngineRunning(std::function<void()> closure);

  EngineBuilder& addStatsSinks(const std::vector<std::string>& stat_sinks);
  EngineBuilder& addGrpcStatsDomain(const std::string& stats_domain);
  EngineBuilder& addConnectTimeoutSeconds(int connect_timeout_seconds);
  EngineBuilder& addDnsRefreshSeconds(int dns_refresh_seconds);
  EngineBuilder& addDnsFailureRefreshSeconds(int base, int max);
  EngineBuilder& addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds);
  EngineBuilder& addDnsMinRefreshSeconds(int dns_min_refresh_seconds);
  EngineBuilder& addDnsPreresolveHostnames(const std::string& dns_preresolve_hostnames);
  EngineBuilder& addMaxConnectionsPerHost(int max_connections_per_host);
  EngineBuilder& useDnsSystemResolver(bool use_system_resolver);
  EngineBuilder& addH2ConnectionKeepaliveIdleIntervalMilliseconds(
      int h2_connection_keepalive_idle_interval_milliseconds);
  EngineBuilder&
  addH2ConnectionKeepaliveTimeoutSeconds(int h2_connection_keepalive_timeout_seconds);
  EngineBuilder& addStatsFlushSeconds(int stats_flush_seconds);
  EngineBuilder& addVirtualClusters(const std::string& virtual_clusters);
  EngineBuilder& addKeyValueStore(const std::string& name, KeyValueStoreSharedPtr key_value_store);
  EngineBuilder& addStringAccessor(const std::string& name, StringAccessorSharedPtr accessor);
  EngineBuilder& setAppVersion(const std::string& app_version);
  EngineBuilder& setAppId(const std::string& app_id);
  EngineBuilder& setDeviceOs(const std::string& app_id);
  EngineBuilder& setStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds);
  EngineBuilder& setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds);
  EngineBuilder& enableGzip(bool gzip_on);
  EngineBuilder& enableBrotli(bool brotli_on);
  EngineBuilder& enableSocketTagging(bool socket_tagging_on);
  EngineBuilder& enableAdminInterface(bool admin_interface_on);
  EngineBuilder& enableHappyEyeballs(bool happy_eyeballs_on);
  EngineBuilder& enableHttp3(bool http3_on);
  EngineBuilder& enableInterfaceBinding(bool interface_binding_on);
  EngineBuilder& enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on);
  EngineBuilder& enableH2ExtendKeepaliveTimeout(bool h2_extend_keepalive_timeout_on);
  EngineBuilder& enforceTrustChainVerification(bool trust_chain_verification_on);
  EngineBuilder& enablePlatformCertificatesValidation(bool platform_certificates_validation_on);

  // this is separated from build() for the sake of testability
  std::string generateConfigStr() const;

  EngineSharedPtr build();

  // TODO(crockeo): add after filter integration
  // EngineBuilder& addPlatformFilter(name: String = UUID.randomUUID().toString(), factory: () ->
  // Filter):
  // EngineBuilder& addNativeFilter(name: String = UUID.randomUUID().toString(),
  // typedConfig: String):

protected:
  void setOverrideConfigForTests(std::string config) { config_override_for_tests_ = config; }
  void setAdminAddressPathForTests(std::string admin) { admin_address_path_for_tests_ = admin; }

private:
  LogLevel log_level_ = LogLevel::info;
  EngineCallbacksSharedPtr callbacks_;

  std::string config_template_;
  std::string stats_domain_;
  int connect_timeout_seconds_ = 30;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int dns_query_timeout_seconds_ = 25;
  std::string dns_preresolve_hostnames_ = "[]";
  bool use_system_resolver_ = true;
  int h2_connection_keepalive_idle_interval_milliseconds_ = 100000000;
  int h2_connection_keepalive_timeout_seconds_ = 10;
  int stats_flush_seconds_ = 60;
  std::string app_version_ = "unspecified";
  std::string app_id_ = "unspecified";
  std::string device_os_ = "unspecified";
  std::string virtual_clusters_ = "[]";
  std::string config_override_for_tests_ = "";
  std::string admin_address_path_for_tests_ = "";
  int stream_idle_timeout_seconds_ = 15;
  int per_try_idle_timeout_seconds_ = 15;
  bool gzip_filter_ = true;
  bool brotli_filter_ = false;
  bool socket_tagging_filter_ = false;
  bool platform_certificates_validation_on_ = false;

  absl::flat_hash_map<std::string, KeyValueStoreSharedPtr> key_value_stores_{};

  bool admin_interface_enabled_ = false;
  bool enable_happy_eyeballs_ = true;
  bool enable_interface_binding_ = false;
  bool enable_drain_post_dns_refresh_ = false;
  bool enforce_trust_chain_verification_ = true;
  bool h2_extend_keepalive_timeout_ = false;
  bool enable_http3_ = false;
  int dns_min_refresh_seconds_ = 60;
  int max_connections_per_host_ = 7;
  std::vector<std::string> stat_sinks_;

  // TODO(crockeo): add after filter integration
  // std::vector<EnvoyHTTPFilterFactory> http_platform_filter_factories_;
  // std::vector<EnvoyNativeFilterConfig> native_filter_chain_;
  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_;
};

using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;

} // namespace Platform
} // namespace Envoy
