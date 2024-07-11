#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "library/cc/engine.h"
#include "library/cc/key_value_store.h"
#include "library/cc/string_accessor.h"
#include "library/common/engine_types.h"

namespace Envoy {
namespace Platform {

// The C++ Engine builder creates a structured bootstrap proto and modifies it through parameters
// set through the EngineBuilder API calls to produce the Bootstrap config that the Engine is
// created from.
class EngineBuilder {
public:
  EngineBuilder();
  EngineBuilder(EngineBuilder&&) = default;
  virtual ~EngineBuilder() = default;
  static std::string nativeNameToConfig(absl::string_view name);

  EngineBuilder& setLogLevel(Logger::Logger::Levels log_level);
  EngineBuilder& setLogger(std::unique_ptr<EnvoyLogger> logger);
  EngineBuilder& setEngineCallbacks(std::unique_ptr<EngineCallbacks> callbacks);
  EngineBuilder& setOnEngineRunning(absl::AnyInvocable<void()> closure);
  EngineBuilder& setOnEngineExit(absl::AnyInvocable<void()> closure);
  EngineBuilder& setEventTracker(std::unique_ptr<EnvoyEventTracker> event_tracker);
  EngineBuilder& addConnectTimeoutSeconds(int connect_timeout_seconds);
  EngineBuilder& addDnsRefreshSeconds(int dns_refresh_seconds);
  EngineBuilder& addDnsFailureRefreshSeconds(int base, int max);
  EngineBuilder& addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds);
  EngineBuilder& addDnsMinRefreshSeconds(int dns_min_refresh_seconds);
  EngineBuilder& addMaxConnectionsPerHost(int max_connections_per_host);
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
#ifdef ENVOY_ENABLE_QUIC
  EngineBuilder& enableHttp3(bool http3_on);
  EngineBuilder& setHttp3ConnectionOptions(std::string options);
  EngineBuilder& setHttp3ClientConnectionOptions(std::string options);
  EngineBuilder& addQuicHint(std::string host, int port);
  EngineBuilder& addQuicCanonicalSuffix(std::string suffix);
  EngineBuilder& enablePortMigration(bool enable_port_migration);
#endif
  EngineBuilder& enableInterfaceBinding(bool interface_binding_on);
  EngineBuilder& enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on);
  // Sets whether to use GRO for upstream UDP sockets (QUIC/HTTP3).
  EngineBuilder& setUseGroIfAvailable(bool use_gro_if_available);
  EngineBuilder& setSocketReceiveBufferSize(int32_t size);
  EngineBuilder& enforceTrustChainVerification(bool trust_chain_verification_on);
  EngineBuilder& setUpstreamTlsSni(std::string sni);
  EngineBuilder& enablePlatformCertificatesValidation(bool platform_certificates_validation_on);

  EngineBuilder& enableDnsCache(bool dns_cache_on, int save_interval_seconds = 1);
  EngineBuilder& setForceAlwaysUsev6(bool value);
  // Adds the hostnames that should be pre-resolved by DNS prior to the first request issued for
  // that host. When invoked, any previous preresolve hostname entries get cleared and only the ones
  // provided in the hostnames argument get set.
  // TODO(abeyad): change this method and the other language APIs to take a {host,port} pair.
  // E.g. addDnsPreresolveHost(std::string host, uint32_t port);
  EngineBuilder& addDnsPreresolveHostnames(const std::vector<std::string>& hostnames);
  EngineBuilder& addNativeFilter(std::string name, std::string typed_config);

  EngineBuilder& addPlatformFilter(const std::string& name);
  // Adds a runtime guard for the `envoy.reloadable_features.<guard>`.
  // For example if the runtime guard is `envoy.reloadable_features.use_foo`, the guard name is
  // `use_foo`.
  EngineBuilder& addRuntimeGuard(std::string guard, bool value);

  // These functions don't affect the Bootstrap configuration but instead perform registrations.
  EngineBuilder& addKeyValueStore(std::string name, KeyValueStoreSharedPtr key_value_store);
  EngineBuilder& addStringAccessor(std::string name, StringAccessorSharedPtr accessor);

  // Sets the thread priority of the Envoy main (network) thread.
  // The value must be an integer between -20 (highest priority) and 19 (lowest priority). Values
  // outside of this range will be ignored.
  EngineBuilder& setNetworkThreadPriority(int thread_priority);

#if defined(__APPLE__)
  // Right now, this API is only used by Apple (iOS) to register the Apple proxy resolver API for
  // use in reading and using the system proxy settings.
  // If/when we move Android system proxy registration to the C++ Engine Builder, we will make this
  // API available on all platforms.
  EngineBuilder& respectSystemProxySettings(bool value);
#else
  // Only android supports c_ares
  EngineBuilder& setUseCares(bool use_cares);
#endif

  // This is separated from build() for the sake of testability
  virtual std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> generateBootstrap() const;

  EngineSharedPtr build();

private:
  struct NativeFilterConfig {
    NativeFilterConfig(std::string name, std::string typed_config)
        : name_(std::move(name)), typed_config_(std::move(typed_config)) {}

    std::string name_;
    std::string typed_config_;
  };

  Logger::Logger::Levels log_level_ = Logger::Logger::Levels::info;
  std::unique_ptr<EnvoyLogger> logger_{nullptr};
  std::unique_ptr<EngineCallbacks> callbacks_;
  std::unique_ptr<EnvoyEventTracker> event_tracker_{nullptr};

  int connect_timeout_seconds_ = 30;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int dns_query_timeout_seconds_ = 5;
  int h2_connection_keepalive_idle_interval_milliseconds_ = 100000000;
  int h2_connection_keepalive_timeout_seconds_ = 10;
  std::string app_version_ = "unspecified";
  std::string app_id_ = "unspecified";
  std::string device_os_ = "unspecified";
  int stream_idle_timeout_seconds_ = 15;
  int per_try_idle_timeout_seconds_ = 15;
  bool gzip_decompression_filter_ = true;
  bool brotli_decompression_filter_ = false;
  bool socket_tagging_filter_ = false;
  bool platform_certificates_validation_on_ = false;
  bool dns_cache_on_ = false;
  int dns_cache_save_interval_seconds_ = 1;
  absl::optional<int> network_thread_priority_ = absl::nullopt;

  absl::flat_hash_map<std::string, KeyValueStoreSharedPtr> key_value_stores_{};

  bool enable_interface_binding_ = false;
  bool enable_drain_post_dns_refresh_ = false;
  bool enforce_trust_chain_verification_ = true;
  std::string upstream_tls_sni_;
  bool enable_http3_ = true;
#if !defined(__APPLE__)
  bool use_cares_ = false;
#endif
  std::string http3_connection_options_ = "";
  std::string http3_client_connection_options_ = "";
  std::vector<std::pair<std::string, int>> quic_hints_;
  std::vector<std::string> quic_suffixes_;
  bool enable_port_migration_ = false;
  bool always_use_v6_ = false;
#if defined(__APPLE__)
  // TODO(abeyad): once stable, consider setting the default to true.
  bool respect_system_proxy_settings_ = false;
#endif
  int dns_min_refresh_seconds_ = 60;
  int max_connections_per_host_ = 7;

  std::vector<NativeFilterConfig> native_filter_chain_;
  std::vector<std::pair<std::string /* host */, uint32_t /* port */>> dns_preresolve_hostnames_;

  std::vector<std::pair<std::string, bool>> runtime_guards_;
  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_;
  bool use_gro_if_available_ = false;

  // This is the same value Cronet uses for QUIC:
  // https://source.chromium.org/chromium/chromium/src/+/main:net/quic/quic_context.h;drc=ccfe61524368c94b138ddf96ae8121d7eb7096cf;l=87
  int32_t socket_receive_buffer_size_ = 1024 * 1024; // 1MB
};

using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;

} // namespace Platform
} // namespace Envoy
