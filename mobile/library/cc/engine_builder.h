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
#include "direct_response_testing.h"
#include "engine.h"
#include "engine_callbacks.h"
#include "key_value_store.h"
#include "library/common/types/matcher_data.h"
#include "log_level.h"
#include "string_accessor.h"

namespace Envoy {
namespace Platform {

constexpr int DefaultXdsTimeout = 5;

// Forward declaration so it can be referenced by XdsBuilder.
class EngineBuilder;

// Represents the locality information in the Bootstrap's node, as defined in:
// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-locality
struct NodeLocality {
  std::string region;
  std::string zone;
  std::string sub_zone;
};

#ifdef ENVOY_MOBILE_XDS
// A class for building the xDS configuration for the Envoy Mobile engine.
// xDS is a protocol for dynamic configuration of Envoy instances, more information can be found in:
// https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol.
//
// This class is typically used as input to the EngineBuilder's setXds() method.
class XdsBuilder final {
public:
  // `xds_server_address`: the host name or IP address of the xDS management server. The xDS server
  //                       must support the ADS protocol
  //                       (https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/operations/dynamic_configuration#aggregated-xds-ads).
  // `xds_server_port`: the port on which the xDS management server listens for ADS discovery
  //                    requests.
  XdsBuilder(std::string xds_server_address, const uint32_t xds_server_port);

  // Adds a header to the initial HTTP metadata headers sent on the gRPC stream.
  //
  // A common use for the initial metadata headers is for authentication to the xDS management
  // server.
  //
  // For example, if using API keys to authenticate to Traffic Director on GCP (see
  // https://cloud.google.com/docs/authentication/api-keys for details), invoke:
  //   builder.addInitialStreamHeader("x-goog-api-key", api_key_token)
  //          .addInitialStreamHeader("X-Android-Package", app_package_name)
  //          .addInitialStreamHeader("X-Android-Cert", sha1_key_fingerprint);
  XdsBuilder& addInitialStreamHeader(std::string header, std::string value);

  // Sets the PEM-encoded server root certificates used to negotiate the TLS handshake for the gRPC
  // connection. If no root certs are specified, the operating system defaults are used.
  XdsBuilder& setSslRootCerts(std::string root_certs);

  // Adds Runtime Discovery Service (RTDS) to the Runtime layers of the Bootstrap configuration,
  // to retrieve dynamic runtime configuration via the xDS management server.
  //
  // `resource_name`: The runtime config resource to subscribe to.
  // `timeout_in_seconds`: <optional> specifies the `initial_fetch_timeout` field on the
  //    api.v3.core.ConfigSource. Unlike the ConfigSource default of 15s, we set a default fetch
  //    timeout value of 5s, to prevent mobile app initialization from stalling. The default
  //    parameter value may change through the course of experimentation and no assumptions should
  //    be made of its exact value.
  XdsBuilder& addRuntimeDiscoveryService(std::string resource_name,
                                         int timeout_in_seconds = DefaultXdsTimeout);

  // Adds the Cluster Discovery Service (CDS) configuration for retrieving dynamic cluster resources
  // via the xDS management server.
  //
  // `cds_resources_locator`: <optional> the xdstp:// URI for subscribing to the cluster resources.
  //    If not using xdstp, then `cds_resources_locator` should be set to the empty string.
  // `timeout_in_seconds`: <optional> specifies the `initial_fetch_timeout` field on the
  //    api.v3.core.ConfigSource. Unlike the ConfigSource default of 15s, we set a default fetch
  //    timeout value of 5s, to prevent mobile app initialization from stalling. The default
  //    parameter value may change through the course of experimentation and no assumptions should
  //    be made of its exact value.
  XdsBuilder& addClusterDiscoveryService(std::string cds_resources_locator = "",
                                         int timeout_in_seconds = DefaultXdsTimeout);

protected:
  // Sets the xDS configuration specified on this XdsBuilder instance on the Bootstrap proto
  // provided as an input parameter.
  //
  // This method takes in a modifiable Bootstrap proto pointer because returning a new Bootstrap
  // proto would rely on proto's MergeFrom behavior, which can lead to unexpected results in the
  // Bootstrap config.
  void build(envoy::config::bootstrap::v3::Bootstrap& bootstrap) const;

private:
  // Required so that EngineBuilder can call the XdsBuilder's protected build() method.
  friend class EngineBuilder;

  std::string xds_server_address_;
  uint32_t xds_server_port_;
  std::vector<envoy::config::core::v3::HeaderValue> xds_initial_grpc_metadata_;
  std::string ssl_root_certs_;
  std::string rtds_resource_name_;
  int rtds_timeout_in_seconds_ = DefaultXdsTimeout;
  bool enable_cds_ = false;
  std::string cds_resources_locator_;
  int cds_timeout_in_seconds_ = DefaultXdsTimeout;
};
#endif

// The C++ Engine builder creates a structured bootstrap proto and modifies it through parameters
// set through the EngineBuilder API calls to produce the Bootstrap config that the Engine is
// created from.
class EngineBuilder {
public:
  EngineBuilder();
  virtual ~EngineBuilder() {}

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
#ifdef ENVOY_ENABLE_QUIC
  EngineBuilder& enableHttp3(bool http3_on);
  EngineBuilder& setHttp3ConnectionOptions(std::string options);
  EngineBuilder& setHttp3ClientConnectionOptions(std::string options);
  EngineBuilder& addQuicHint(std::string host, int port);
  EngineBuilder& addQuicCanonicalSuffix(std::string suffix);
#endif
  EngineBuilder& enableInterfaceBinding(bool interface_binding_on);
  EngineBuilder& enableDrainPostDnsRefresh(bool drain_post_dns_refresh_on);
  EngineBuilder& enforceTrustChainVerification(bool trust_chain_verification_on);
  EngineBuilder& enablePlatformCertificatesValidation(bool platform_certificates_validation_on);
  // Sets the node.id field in the Bootstrap configuration.
  EngineBuilder& setNodeId(std::string node_id);
  // Sets the node.locality field in the Bootstrap configuration.
  EngineBuilder& setNodeLocality(std::string region, std::string zone, std::string sub_zone);
  // Sets the node.metadata field in the Bootstrap configuration.
  EngineBuilder& setNodeMetadata(ProtobufWkt::Struct node_metadata);
#ifdef ENVOY_MOBILE_XDS
  // Sets the xDS configuration for the Envoy Mobile engine.
  //
  // `xds_builder`: the XdsBuilder instance used to specify the xDS configuration options.
  EngineBuilder& setXds(XdsBuilder xds_builder);
#endif
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

  EngineBuilder& setRuntimeGuard(std::string guard, bool value);

  // These functions don't affect the Bootstrap configuration but instead perform registrations.
  EngineBuilder& addKeyValueStore(std::string name, KeyValueStoreSharedPtr key_value_store);
  EngineBuilder& addStringAccessor(std::string name, StringAccessorSharedPtr accessor);

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

  LogLevel log_level_ = LogLevel::info;
  EngineCallbacksSharedPtr callbacks_;

  int connect_timeout_seconds_ = 30;
  int dns_refresh_seconds_ = 60;
  int dns_failure_refresh_seconds_base_ = 2;
  int dns_failure_refresh_seconds_max_ = 10;
  int dns_query_timeout_seconds_ = 25;
  bool use_system_resolver_ = true;
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
  std::string node_id_;
  absl::optional<NodeLocality> node_locality_ = absl::nullopt;
  absl::optional<ProtobufWkt::Struct> node_metadata_ = absl::nullopt;
  bool dns_cache_on_ = false;
  int dns_cache_save_interval_seconds_ = 1;

  absl::flat_hash_map<std::string, KeyValueStoreSharedPtr> key_value_stores_{};

  bool enable_interface_binding_ = false;
  bool enable_drain_post_dns_refresh_ = false;
  bool enforce_trust_chain_verification_ = true;
  bool enable_http3_ = true;
  std::string http3_connection_options_ = "";
  std::string http3_client_connection_options_ = "";
  std::vector<std::pair<std::string, int>> quic_hints_;
  std::vector<std::string> quic_suffixes_;
  bool always_use_v6_ = false;
  int dns_min_refresh_seconds_ = 60;
  int max_connections_per_host_ = 7;

  std::vector<NativeFilterConfig> native_filter_chain_;
  std::vector<std::pair<std::string /* host */, uint32_t /* port */>> dns_preresolve_hostnames_;

  std::vector<std::pair<std::string, bool>> runtime_guards_;
  absl::flat_hash_map<std::string, StringAccessorSharedPtr> string_accessors_;

#ifdef ENVOY_MOBILE_XDS
  absl::optional<XdsBuilder> xds_builder_ = absl::nullopt;
#endif
};

using EngineBuilderSharedPtr = std::shared_ptr<EngineBuilder>;

} // namespace Platform
} // namespace Envoy
