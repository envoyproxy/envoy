#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "direct_response_testing.h"
#include "engine.h"
#include "engine_callbacks.h"
#include "key_value_store.h"
#include "library/common/types/matcher_data.h"
#include "log_level.h"
#include "string_accessor.h"

namespace Envoy {
namespace Platform {

constexpr int DefaultJwtTokenLifetimeSeconds = 31536000; // 1 year
constexpr int DefaultXdsTimeout = 5;

// Represents the locality information in the Bootstrap's node, as defined in:
// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-locality
struct NodeLocality {
  std::string region;
  std::string zone;
  std::string sub_zone;
};

// The C++ Engine builder creates a structured bootstrap proto and modifies it through parameters
// set through the EngineBuilder API calls to produce the Bootstrap config that the Engine is
// created from.
class EngineBuilder {
public:
  EngineBuilder();

  EngineBuilder& addLogLevel(LogLevel log_level);
  EngineBuilder& setOnEngineRunning(std::function<void()> closure);
  EngineBuilder& addConnectTimeoutSeconds(int connect_timeout_seconds);
  EngineBuilder& addDnsRefreshSeconds(int dns_refresh_seconds);
  EngineBuilder& addDnsFailureRefreshSeconds(int base, int max);
  EngineBuilder& addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds);
  EngineBuilder& addDnsMinRefreshSeconds(int dns_min_refresh_seconds);
  EngineBuilder& addMaxConnectionsPerHost(int max_connections_per_host);
  EngineBuilder& useDnsSystemResolver(bool use_system_resolver);
  EngineBuilder& addH2ConnectionKeepaliveIdleIntervalMilliseconds(
      int h2_connection_keepalive_idle_interval_milliseconds);
  EngineBuilder&
  addH2ConnectionKeepaliveTimeoutSeconds(int h2_connection_keepalive_timeout_seconds);
  // Configures Envoy to use the PlatformBridge filter named `name`. An instance of
  // envoy_http_filter must be registered as a platform API with the same name.
  EngineBuilder& setAppVersion(std::string app_version);
  EngineBuilder& setAppId(std::string app_id);
  EngineBuilder& setDeviceOs(std::string app_id);
  EngineBuilder& setStreamIdleTimeoutSeconds(int stream_idle_timeout_seconds);
  EngineBuilder& setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds);
  EngineBuilder& enableGzipDecompression(bool gzip_decompression_on);
  EngineBuilder& enableBrotliDecompression(bool brotli_decompression_on);
  EngineBuilder& enableSocketTagging(bool socket_tagging_on);
  EngineBuilder& enableHappyEyeballs(bool happy_eyeballs_on);
#ifdef ENVOY_ENABLE_QUIC
  EngineBuilder& enableHttp3(bool http3_on);
#endif
  EngineBuilder& enableInterfaceBinding(bool interface_binding_on);
  EngineBuilder& enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on);
  EngineBuilder& enforceTrustChainVerification(bool trust_chain_verification_on);
  EngineBuilder& enablePlatformCertificatesValidation(bool platform_certificates_validation_on);
#ifdef ENVOY_GOOGLE_GRPC
  // Sets the node.id field in the Bootstrap configuration.
  EngineBuilder& setNodeId(std::string node_id);
  // Sets the node.locality field in the Bootstrap configuration.
  EngineBuilder& setNodeLocality(std::string region, std::string zone, std::string sub_zone);
  // Adds an ADS layer. Note that only the state-of-the-world gRPC protocol is supported, not Delta
  // gRPC.
  EngineBuilder& setAggregatedDiscoveryService(std::string address, const int port,
                                               std::string jwt_token = "",
                                               int jwt_token_lifetime_seconds = 0,
                                               std::string ssl_root_certs = "");
  // Adds an RTDS layer to default config. Requires that ADS be configured.
  EngineBuilder& addRtdsLayer(std::string layer_name, const int timeout_seconds = 0);
  // Adds a CDS layer to default config. Requires that ADS be configured via
  // setAggregatedDiscoveryService(). If `cds_resources_locator` is non-empty, the xdstp namespace
  // is used for identifying resources. If not using xdstp, then set `cds_resources_locator` to the
  // empty string.
  EngineBuilder& addCdsLayer(std::string cds_resources_locator = "", const int timeout_seconds = 0);
#endif
  EngineBuilder& enableDnsCache(bool dns_cache_on, int save_interval_seconds = 1);
  EngineBuilder& setForceAlwaysUsev6(bool value);
  EngineBuilder& setSkipDnsLookupForProxiedRequests(bool value);
  EngineBuilder& addDnsPreresolveHostnames(const std::vector<std::string>& hostnames);
  EngineBuilder& addNativeFilter(std::string name, std::string typed_config);
#ifdef ENVOY_ADMIN_FUNCTIONALITY
  EngineBuilder& enableAdminInterface(bool admin_interface_on);
#endif

#ifdef ENVOY_MOBILE_STATS_REPORTING
  EngineBuilder& addStatsSinks(std::vector<std::string> stat_sinks);
  EngineBuilder& addGrpcStatsDomain(std::string stats_domain);
  EngineBuilder& addStatsFlushSeconds(int stats_flush_seconds);
#endif
  EngineBuilder& addPlatformFilter(std::string name);
  // TODO(alyssawilk) remove the legacy APIs and update docs once Lyft is moved over.
  EngineBuilder& addVirtualCluster(std::string virtual_cluster);

  EngineBuilder& addVirtualCluster(std::string name, std::vector<MatcherData> matchers);
  EngineBuilder& setRuntimeGuard(std::string guard, bool value);

  // Add a direct response. For testing purposes only.
  // TODO(jpsim): Move this out of the main engine builder API
  EngineBuilder& addDirectResponse(DirectResponseTesting::DirectResponse direct_response);

  // These functions don't affect the Bootstrap configuration but instead perform registrations.
  EngineBuilder& addKeyValueStore(std::string name, KeyValueStoreSharedPtr key_value_store);
  EngineBuilder& addStringAccessor(std::string name, StringAccessorSharedPtr accessor);

  // This is separated from build() for the sake of testability
  std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> generateBootstrap() const;

  EngineSharedPtr build();

protected:
  // Overrides the EngineBuilder's constructed configuration with the YAML config supplied in the
  // `config` parameter. Any prior or subsequent calls to the EngineBuilder's APIs are ignored in
  // favor of using the YAML string to construct the Envoy Engine's Bootsrap configuration.
  //
  // This method is protected so that production code does not rely on it.
  //
  // Tests that need access to this method should go through the TestEngineBuilder class, which
  // provides access to this method.
  EngineBuilder& setOverrideConfigForTests(std::string config);

private:
  struct NativeFilterConfig {
    NativeFilterConfig(std::string name, std::string typed_config)
        : name_(std::move(name)), typed_config_(std::move(typed_config)) {}

    std::string name_;
    std::string typed_config_;
  };

  LogLevel log_level_ = LogLevel::info;
  EngineCallbacksSharedPtr callbacks_;

  std::string stats_domain_;
  int connect_timeout_seconds_ = 30;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int dns_query_timeout_seconds_ = 25;
  bool use_system_resolver_ = true;
  int h2_connection_keepalive_idle_interval_milliseconds_ = 100000000;
  int h2_connection_keepalive_timeout_seconds_ = 10;
  int stats_flush_seconds_ = 60;
  std::string app_version_ = "unspecified";
  std::string app_id_ = "unspecified";
  std::string device_os_ = "unspecified";
  std::string config_override_for_tests_ = "";
  int stream_idle_timeout_seconds_ = 15;
  int per_try_idle_timeout_seconds_ = 15;
  bool gzip_decompression_filter_ = true;
  bool brotli_decompression_filter_ = false;
  bool socket_tagging_filter_ = false;
  bool platform_certificates_validation_on_ = false;
  std::string rtds_layer_name_ = "";
  int rtds_timeout_seconds_;
  std::string node_id_;
  absl::optional<NodeLocality> node_locality_ = absl::nullopt;
  std::string ads_address_ = ""; // make absl:optional?
  int ads_port_;
  std::string ads_jwt_token_;
  int ads_jwt_token_lifetime_seconds_;
  std::string ads_ssl_root_certs_;
  std::string cds_resources_locator_;
  int cds_timeout_seconds_;
  bool enable_cds_ = false;
  bool dns_cache_on_ = false;
  int dns_cache_save_interval_seconds_ = 1;

  absl::flat_hash_map<std::string, KeyValueStoreSharedPtr> key_value_stores_{};

  bool admin_interface_enabled_ = false;
  bool enable_happy_eyeballs_ = true;
  bool enable_interface_binding_ = false;
  bool enable_drain_post_dns_refresh_ = false;
  bool enforce_trust_chain_verification_ = true;
  bool enable_http3_ = true;
  bool always_use_v6_ = false;
  int dns_min_refresh_seconds_ = 60;
  int max_connections_per_host_ = 7;
  std::vector<std::string> stats_sinks_;

  std::vector<NativeFilterConfig> native_filter_chain_;
  std::vector<std::string> dns_preresolve_hostnames_;
  std::vector<std::string> virtual_clusters_;
  std::vector<std::pair<std::string, std::vector<MatcherData>>> virtual_cluster_data_;
  std::vector<DirectResponseTesting::DirectResponse> direct_responses_;

  std::vector<std::pair<std::string, bool>> runtime_guards_;
  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_;
  bool skip_dns_lookups_for_proxied_requests_ = false;
};

using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;

} // namespace Platform
} // namespace Envoy
