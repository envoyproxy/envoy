#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/core/v3/socket_option.pb.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "library/cc/engine.h"
#include "library/cc/engine_builder_base.h"
#include "library/cc/key_value_store.h"
#include "library/cc/string_accessor.h"
#include "library/common/engine_types.h"

#include "library/cc/engine_builder_types.h"

namespace Envoy {
namespace Platform {

// The C++ Engine builder creates a structured bootstrap proto and modifies it through parameters
// set through the MobileEngineBuilder API calls to produce the Bootstrap config that the Engine is
// created from.
class MobileEngineBuilder : public Platform::EngineBuilderBase<MobileEngineBuilder> {
public:
  MobileEngineBuilder();
  MobileEngineBuilder(MobileEngineBuilder&&) = default;
  virtual ~MobileEngineBuilder() = default;
  static std::string nativeNameToConfig(absl::string_view name);

  MobileEngineBuilder& addConnectTimeoutSeconds(int connect_timeout_seconds);
  MobileEngineBuilder& addDnsRefreshSeconds(int dns_refresh_seconds);
  MobileEngineBuilder& addDnsFailureRefreshSeconds(int base, int max);
  MobileEngineBuilder& addDnsQueryTimeoutSeconds(int dns_query_timeout_seconds);
  MobileEngineBuilder& setDisableDnsRefreshOnFailure(bool disable_dns_refresh_on_failure);
  MobileEngineBuilder&
  setDisableDnsRefreshOnNetworkChange(bool disable_dns_refresh_on_network_change);

  MobileEngineBuilder& addDnsMinRefreshSeconds(int dns_min_refresh_seconds);
  MobileEngineBuilder& setDnsNumRetries(uint32_t dns_num_retries);
  MobileEngineBuilder& setGetaddrinfoNumThreads(uint32_t num_threads);
  MobileEngineBuilder& addMaxConnectionsPerHost(int max_connections_per_host);
  MobileEngineBuilder& addH2ConnectionKeepaliveIdleIntervalMilliseconds(
      int h2_connection_keepalive_idle_interval_milliseconds);
  MobileEngineBuilder&
  addH2ConnectionKeepaliveTimeoutSeconds(int h2_connection_keepalive_timeout_seconds);
  MobileEngineBuilder& setAppVersion(std::string app_version);
  MobileEngineBuilder& setAppId(std::string app_id);
  MobileEngineBuilder& setDeviceOs(std::string app_id);

  MobileEngineBuilder& setPerTryIdleTimeoutSeconds(int per_try_idle_timeout_seconds);
  MobileEngineBuilder& enableGzipDecompression(bool gzip_decompression_on);
  MobileEngineBuilder& enableBrotliDecompression(bool brotli_decompression_on);
  MobileEngineBuilder& enableSocketTagging(bool socket_tagging_on);
  MobileEngineBuilder& enableHttp3(bool http3_on);
  MobileEngineBuilder& enableEarlyData(bool early_data_on);
  MobileEngineBuilder& enableScone(bool enable);
  MobileEngineBuilder& addQuicConnectionOption(std::string option);
  MobileEngineBuilder& addQuicClientConnectionOption(std::string option);
  // Deprecated, use addQuicConnectionOption() instead.
  MobileEngineBuilder& setHttp3ConnectionOptions(std::string options);
  // Deprecated, use addQuicClientConnectionOption() instead.
  MobileEngineBuilder& setHttp3ClientConnectionOptions(std::string options);
  MobileEngineBuilder& addQuicHint(std::string host, int port);
  MobileEngineBuilder& addQuicCanonicalSuffix(std::string suffix);
  // 0 means port migration is disabled.
  MobileEngineBuilder& setNumTimeoutsToTriggerPortMigration(int num_timeouts);
  MobileEngineBuilder& enableInterfaceBinding(bool interface_binding_on);
  MobileEngineBuilder& enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on);
  MobileEngineBuilder& setUdpSocketReceiveBufferSize(int32_t size);
  MobileEngineBuilder& setUdpSocketSendBufferSize(int32_t size);
  MobileEngineBuilder& enforceTrustChainVerification(bool trust_chain_verification_on);
  MobileEngineBuilder& setUpstreamTlsSni(std::string sni);
  MobileEngineBuilder&
  enablePlatformCertificatesValidation(bool platform_certificates_validation_on);
  // Overridden to turn off system proxying and platform certs validation while enabling worker
  // thread.
  MobileEngineBuilder& enableWorkerThread(bool use_worker_thread);
  MobileEngineBuilder& setUseQuicPlatformPacketWriter(bool use_quic_platform_packet_writer);
  // If called to enable QUIC connection migration, no need to call setUseQuicPlatformPacketWriter()
  // separately.
  MobileEngineBuilder& enableQuicConnectionMigration(bool quic_connection_migration_on);
  MobileEngineBuilder& setMigrateIdleQuicConnection(bool migrate_idle_quic_connection);
  // 0 means using the Envoy default 30s.
  MobileEngineBuilder&
  setMaxIdleTimeBeforeQuicMigrationSeconds(int max_idle_time_before_quic_migration);
  // 0 means using the Envoy default 128s.
  MobileEngineBuilder& setMaxTimeOnNonDefaultNetworkSeconds(int max_time_on_non_default_network);

  MobileEngineBuilder& enableDnsCache(bool dns_cache_on, int save_interval_seconds = 1);
  // Set additional socket options on the upstream cluster outbound sockets.
  MobileEngineBuilder& setAdditionalSocketOptions(
      const std::vector<envoy::config::core::v3::SocketOption>& socket_options);
  // Adds the hostnames that should be pre-resolved by DNS prior to the first request issued for
  // that host. When invoked, any previous preresolve hostname entries get cleared and only the ones
  // provided in the hostnames argument get set.
  // TODO(abeyad): change this method and the other language APIs to take a {host,port} pair.
  // E.g. addDnsPreresolveHost(std::string host, uint32_t port);
  MobileEngineBuilder& addDnsPreresolveHostnames(const std::vector<std::string>& hostnames);
  MobileEngineBuilder&
  setDnsResolver(const envoy::config::core::v3::TypedExtensionConfig& dns_resolver_config);
  MobileEngineBuilder& addNativeFilter(std::string name, std::string typed_config);
  MobileEngineBuilder& addNativeFilter(const std::string& name, const Protobuf::Any& typed_config);

  MobileEngineBuilder& addPlatformFilter(const std::string& name);
  // Adds a runtime guard for the `envoy.reloadable_features.<guard>`.
  // For example if the runtime guard is `envoy.reloadable_features.use_foo`, the guard name is
  // `use_foo`.
  MobileEngineBuilder& addRuntimeGuard(std::string guard, bool value);

  // These functions don't affect the Bootstrap configuration but instead perform registrations.
  MobileEngineBuilder& addKeyValueStore(std::string name, KeyValueStoreSharedPtr key_value_store);
  MobileEngineBuilder& addStringAccessor(std::string name, StringAccessorSharedPtr accessor);

  // Sets the thread priority of the Envoy main (network) thread.
  // The value must be an integer between -20 (highest priority) and 19 (lowest priority). Values
  // outside of this range will be ignored.
  MobileEngineBuilder& setNetworkThreadPriority(int thread_priority);

  // Sets the QUIC connection idle timeout in seconds.
  MobileEngineBuilder&
  setQuicConnectionIdleTimeoutSeconds(int quic_connection_idle_timeout_seconds);

  // Sets the QUIC connection keepalive initial interval in nanoseconds
  MobileEngineBuilder& setKeepAliveInitialIntervalMilliseconds(int keepalive_initial_interval_ms);

  // Sets the maximum number of concurrent streams on a multiplexed connection (HTTP/2 or HTTP/3).
  MobileEngineBuilder& setMaxConcurrentStreams(int max_concurrent_streams);

  // Sets the node.id field in the Bootstrap configuration.
  MobileEngineBuilder& setNodeId(std::string node_id);
  // Sets the node.locality field in the Bootstrap configuration.
  MobileEngineBuilder& setNodeLocality(std::string region, std::string zone, std::string sub_zone);
  // Sets the node.metadata field in the Bootstrap configuration.
  MobileEngineBuilder& setNodeMetadata(Protobuf::Struct node_metadata);

#if defined(__APPLE__)
  // If true, initialize the platform network change monitor to listen for network change events.
  // Only takes effect on iOS, where it is required in order to enable the network change monitor.
  // Defaults to false.
  MobileEngineBuilder& enableNetworkChangeMonitor(bool network_change_monitor_on);
#endif

#ifdef ENVOY_MOBILE_XDS
  // Sets the xDS configuration for the Envoy Mobile engine.
  //
  // `xds_builder`: the XdsBuilder instance used to specify the xDS configuration options.
  MobileEngineBuilder& setXds(XdsBuilder xds_builder);
#endif // ENVOY_MOBILE_XDS

#if defined(__APPLE__)
  // Right now, this API is only used by Apple (iOS) to register the Apple proxy resolver API for
  // use in reading and using the system proxy settings.
  // If/when we move Android system proxy registration to the C++ Engine Builder, we will make this
  // API available on all platforms.
  // The optional `refresh_interval_secs` parameter determines how often the system proxy settings
  // are polled by the operating system; defaults to 10 seconds. If the value is <= 0, the default
  // value will be used.
  MobileEngineBuilder& respectSystemProxySettings(bool value, int refresh_interval_secs = 10);
  MobileEngineBuilder& setIosNetworkServiceType(int ios_network_service_type);
#endif
  // Overload to preserve the same return type as the EngineBuilder class.
  EngineSharedPtr build();

private:
  friend class Platform::EngineBuilderBase<MobileEngineBuilder>;

  // base class hooks
  void preRunSetup(InternalEngine* engine);
  void postRunSetup(Engine* engine);
  absl::Status configXds(envoy::config::bootstrap::v3::Bootstrap* bootstrap);
  void configureDnsCache(
      envoy::extensions::common::dynamic_forward_proxy::v3::DnsCacheConfig* dns_cache_config) const;
  absl::Status configureNode(envoy::config::core::v3::Node* node);
  absl::Status configureRouteConfig(envoy::config::route::v3::RouteConfiguration* route_config);
  absl::Status configureStaticClusters(
      Protobuf::RepeatedPtrField<envoy::config::cluster::v3::Cluster>* clusters);
  absl::Status configureHttpFilters(
      std::function<envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter*()>
          add_filter);
  void configureCustomRouterFilter(
      ::envoy::extensions::filters::http::router::v3::Router& router_config) {
    (void)router_config;
  }

  void addWatchdog();
  void addStatsInclusionStringMatchers();

  struct NativeFilterConfig {
    NativeFilterConfig(std::string name, std::string typed_config)
        : name_(std::move(name)), textproto_typed_config_(std::move(typed_config)) {}

    NativeFilterConfig(const std::string& name, const Protobuf::Any& typed_config)
        : name_(name), typed_config_(typed_config) {}

    std::string name_;
    std::string textproto_typed_config_{};
    Protobuf::Any typed_config_{};
  };

  int connect_timeout_seconds_ = 10;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int dns_query_timeout_seconds_ = 120;
  bool disable_dns_refresh_on_failure_{false};
  bool disable_dns_refresh_on_network_change_{false};
  absl::optional<uint32_t> dns_num_retries_ = 3;
  uint32_t getaddrinfo_num_threads_ = 1;
  int h2_connection_keepalive_idle_interval_milliseconds_ = 100000000;
  int h2_connection_keepalive_timeout_seconds_ = 15;
  std::string app_version_ = "unspecified";
  std::string app_id_ = "unspecified";
  std::string device_os_ = "unspecified";
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
  bool enable_early_data_{true};
  bool scone_enabled_ = false;
  std::string http3_connection_options_ = "";
  std::string http3_client_connection_options_ = "";
  // EVMB is to distinguish Envoy Mobile client connections.
  std::vector<std::string> quic_connection_options_{"AKDU", "BWRS", "5RTO", "EVMB"};
  std::vector<std::string> quic_client_connection_options_;
  std::vector<std::pair<std::string, int>> quic_hints_;
  std::vector<std::string> quic_suffixes_;
  int num_timeouts_to_trigger_port_migration_ = 0;
#if defined(__APPLE__)
  bool respect_system_proxy_settings_ = true;
  int proxy_settings_refresh_interval_secs_ = 10;
  int ios_network_service_type_ = 0;
#endif
  int dns_min_refresh_seconds_ = 60;
  int max_connections_per_host_ = 7;

  std::vector<NativeFilterConfig> native_filter_chain_;
  std::vector<std::pair<std::string /* host */, uint32_t /* port */>> dns_preresolve_hostnames_;
  absl::optional<envoy::config::core::v3::TypedExtensionConfig> dns_resolver_config_;
  std::vector<envoy::config::core::v3::SocketOption> socket_options_;

  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_;

  // This is the same value Cronet uses for QUIC:
  // https://source.chromium.org/chromium/chromium/src/+/main:net/quic/quic_context.h;drc=ccfe61524368c94b138ddf96ae8121d7eb7096cf;l=87
  int32_t udp_socket_receive_buffer_size_ = 1024 * 1024; // 1MB
  // This is the same value Cronet uses for QUIC:
  // https://source.chromium.org/chromium/chromium/src/+/main:net/quic/quic_session_pool.cc;l=790-793;drc=7f04a8e033c23dede6beae129cd212e6d4473d72
  // https://source.chromium.org/chromium/chromium/src/+/main:net/third_party/quiche/src/quiche/quic/core/quic_constants.h;l=43-47;drc=34ad7f3844f882baf3d31a6bc6e300acaa0e3fc8
  int32_t udp_socket_send_buffer_size_ = 1452 * 20;
  // These are the same values Cronet uses for QUIC:
  // https://source.chromium.org/chromium/chromium/src/+/main:net/quic/quic_context.cc;l=21-22;drc=6849bf6b37e96bd1c38a5f77f7deaa28b53779c4;bpv=1;bpt=1
  const uint32_t initial_stream_window_size_ = 6 * 1024 * 1024;      // 6MB
  const uint32_t initial_connection_window_size_ = 15 * 1024 * 1024; // 15MB
  int quic_connection_idle_timeout_seconds_ = 60;

  int keepalive_initial_interval_ms_ = 0;
  int max_concurrent_streams_ = 0;
  bool use_quic_platform_packet_writer_ = false;

  // QUIC connection migration.
  bool enable_quic_connection_migration_ = false;
  bool migrate_idle_quic_connection_ = false;
  int max_idle_time_before_quic_migration_seconds_ = 0;
  int max_time_on_non_default_network_seconds_ = 0;

  std::string node_id_;
  absl::optional<NodeLocality> node_locality_ = absl::nullopt;
  absl::optional<Protobuf::Struct> node_metadata_ = absl::nullopt;

  bool enable_network_change_monitor_ = false;
#ifdef ENVOY_MOBILE_XDS
  absl::optional<XdsBuilder> xds_builder_ = absl::nullopt;
#endif // ENVOY_MOBILE_XDS
};

using MobileEngineBuilderSharedPtr = std::shared_ptr<MobileEngineBuilder>;

} // namespace Platform
} // namespace Envoy
