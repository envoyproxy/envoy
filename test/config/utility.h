#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/listener/v3/listener_components.pb.h"
#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/http/codes.h"

#include "common/config/api_version.h"
#include "common/network/address_impl.h"
#include "common/protobuf/protobuf.h"

#include "test/integration/server_stats.h"

#include "absl/types/optional.h"

namespace Envoy {

class ConfigHelper {
public:
  using HttpConnectionManager =
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;
  struct ServerSslOptions {
    ServerSslOptions& setRsaCert(bool rsa_cert) {
      rsa_cert_ = rsa_cert;
      return *this;
    }

    ServerSslOptions& setRsaCertOcspStaple(bool rsa_cert_ocsp_staple) {
      rsa_cert_ocsp_staple_ = rsa_cert_ocsp_staple;
      return *this;
    }

    ServerSslOptions& setEcdsaCert(bool ecdsa_cert) {
      ecdsa_cert_ = ecdsa_cert;
      return *this;
    }

    ServerSslOptions& setEcdsaCertOcspStaple(bool ecdsa_cert_ocsp_staple) {
      ecdsa_cert_ocsp_staple_ = ecdsa_cert_ocsp_staple;
      return *this;
    }

    ServerSslOptions& setOcspStapleRequired(bool ocsp_staple_required) {
      ocsp_staple_required_ = ocsp_staple_required;
      return *this;
    }

    ServerSslOptions& setTlsV13(bool tlsv1_3) {
      tlsv1_3_ = tlsv1_3;
      return *this;
    }

    ServerSslOptions& setExpectClientEcdsaCert(bool expect_client_ecdsa_cert) {
      expect_client_ecdsa_cert_ = expect_client_ecdsa_cert;
      return *this;
    }

    bool rsa_cert_{true};
    bool rsa_cert_ocsp_staple_{true};
    bool ecdsa_cert_{false};
    bool ecdsa_cert_ocsp_staple_{false};
    bool ocsp_staple_required_{false};
    bool tlsv1_3_{false};
    bool expect_client_ecdsa_cert_{false};
  };

  // Set up basic config, using the specified IpVersion for all connections: listeners, upstream,
  // and admin connections.
  //
  // By default, this runs with an L7 proxy config, but config can be set to TCP_PROXY_CONFIG
  // to test L4 proxying.
  ConfigHelper(const Network::Address::IpVersion version, Api::Api& api,
               const std::string& config = httpProxyConfig());

  static void
  initializeTls(const ServerSslOptions& options,
                envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_context);

  using ConfigModifierFunction = std::function<void(envoy::config::bootstrap::v3::Bootstrap&)>;
  using HttpModifierFunction = std::function<void(HttpConnectionManager&)>;

  // A basic configuration (admin port, cluster_0, one listener) with no network filters.
  static std::string baseConfig();

  // A basic configuration (admin port, cluster_0, one udp listener) with no network filters.
  static std::string baseUdpListenerConfig();

  // A string for a tls inspector listener filter which can be used with addListenerFilter()
  static std::string tlsInspectorFilter();

  // A basic configuration for L4 proxying.
  static std::string tcpProxyConfig();
  // A basic configuration for L7 proxying.
  static std::string httpProxyConfig();
  // A basic configuration for L7 proxying with QUIC transport.
  static std::string quicHttpProxyConfig();
  // A string for a basic buffer filter, which can be used with addFilter()
  static std::string defaultBufferFilter();
  // A string for a small buffer filter, which can be used with addFilter()
  static std::string smallBufferFilter();
  // A string for a health check filter which can be used with addFilter()
  static std::string defaultHealthCheckFilter();
  // A string for a squash filter which can be used with addFilter()
  static std::string defaultSquashFilter();

  // Configuration for L7 proxying, with clusters cluster_1 and cluster_2 meant to be added via CDS.
  // api_type should be REST, GRPC, or DELTA_GRPC.
  static std::string discoveredClustersBootstrap(const std::string& api_type);
  static std::string adsBootstrap(const std::string& api_type,
                                  envoy::config::core::v3::ApiVersion api_version);
  // Builds a standard Cluster config fragment, with a single endpoint (at address:port).
  static envoy::config::cluster::v3::Cluster buildStaticCluster(const std::string& name, int port,
                                                                const std::string& address);

  // ADS configurations
  static envoy::config::cluster::v3::Cluster buildCluster(
      const std::string& name, const std::string& lb_policy = "ROUND_ROBIN",
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  static envoy::config::cluster::v3::Cluster buildTlsCluster(
      const std::string& name, const std::string& lb_policy = "ROUND_ROBIN",
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  static envoy::config::endpoint::v3::ClusterLoadAssignment buildClusterLoadAssignment(
      const std::string& name, const std::string& ip_version, uint32_t port,
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  static envoy::config::listener::v3::Listener buildBaseListener(
      const std::string& name, const std::string& address, const std::string& filter_chains = "",
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  static envoy::config::listener::v3::Listener buildListener(
      const std::string& name, const std::string& route_config, const std::string& address,
      const std::string& stat_prefix,
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  static envoy::config::route::v3::RouteConfiguration buildRouteConfig(
      const std::string& name, const std::string& cluster,
      envoy::config::core::v3::ApiVersion api_version = envoy::config::core::v3::ApiVersion::V3);

  // Builds a standard Endpoint suitable for population by finalize().
  static envoy::config::endpoint::v3::Endpoint buildEndpoint(const std::string& address);

  // Run the final config modifiers, and then set the upstream ports based on upstream connections.
  // This is the last operation run on |bootstrap_| before it is handed to Envoy.
  // Ports are assigned by looping through clusters, hosts, and addresses in the
  // order they are stored in |bootstrap_|
  void finalize(const std::vector<uint32_t>& ports);

  // Set source_address in the bootstrap bind config.
  void setSourceAddress(const std::string& address_string);

  // Overwrite the first host and route for the primary listener.
  void setDefaultHostAndRoute(const std::string& host, const std::string& route);

  // Sets byte limits on upstream and downstream connections.
  void setBufferLimits(uint32_t upstream_buffer_limit, uint32_t downstream_buffer_limit);

  // Set the idle timeout on downstream connections through the HttpConnectionManager.
  void setDownstreamHttpIdleTimeout(std::chrono::milliseconds idle_timeout);

  // Set the max connection duration for downstream connections through the HttpConnectionManager.
  void setDownstreamMaxConnectionDuration(std::chrono::milliseconds max_connection_duration);

  // Set the max stream duration for downstream connections through the HttpConnectionManager.
  void setDownstreamMaxStreamDuration(std::chrono::milliseconds max_stream_duration);

  // Set the connect timeout on upstream connections.
  void setConnectTimeout(std::chrono::milliseconds timeout);

  envoy::config::route::v3::VirtualHost createVirtualHost(const char* host, const char* route = "/",
                                                          const char* cluster = "cluster_0");

  void addVirtualHost(const envoy::config::route::v3::VirtualHost& vhost);

  // Add an HTTP filter prior to existing filters.
  void addFilter(const std::string& filter_yaml);

  // Add a network filter prior to existing filters.
  void addNetworkFilter(const std::string& filter_yaml);

  // Add a listener filter prior to existing filters.
  void addListenerFilter(const std::string& filter_yaml);

  // Sets the client codec to the specified type.
  void setClientCodec(envoy::extensions::filters::network::http_connection_manager::v3::
                          HttpConnectionManager::CodecType type);

  // Add the default SSL configuration.
  void addSslConfig(const ServerSslOptions& options);
  void addSslConfig() { addSslConfig({}); }

  // Set the HTTP access log for the first HCM (if present) to a given file. The default is
  // the platform's null device.
  bool setAccessLog(const std::string& filename, absl::string_view format = "");

  // Set the listener access log for the first listener to a given file.
  bool setListenerAccessLog(const std::string& filename, absl::string_view format = "");

  // Renames the first listener to the name specified.
  void renameListener(const std::string& name);

  // Allows callers to do their own modification to |bootstrap_| which will be
  // applied just before ports are modified in finalize().
  void addConfigModifier(ConfigModifierFunction function);

  // Allows callers to easily modify the HttpConnectionManager configuration.
  // Modifiers will be applied just before ports are modified in finalize
  void addConfigModifier(HttpModifierFunction function);

  // Apply any outstanding config modifiers, stick all the listeners in a discovery response message
  // and write it to the lds file.
  void setLds(absl::string_view version_info);

  // Set limits on pending downstream outbound frames.
  void setDownstreamOutboundFramesLimits(uint32_t max_all_frames, uint32_t max_control_frames);

  // Set limits on pending upstream outbound frames.
  void setUpstreamOutboundFramesLimits(uint32_t max_all_frames, uint32_t max_control_frames);

  // Return the bootstrap configuration for hand-off to Envoy.
  const envoy::config::bootstrap::v3::Bootstrap& bootstrap() { return bootstrap_; }

  // Allow a finalized configuration to be edited for generating xDS responses
  void applyConfigModifiers();

  // Skip validation that ensures that all upstream ports are referenced by the
  // configuration generated in ConfigHelper::finalize.
  void skipPortUsageValidation() { skip_port_usage_validation_ = true; }

  // Add this key value pair to the static runtime.
  void addRuntimeOverride(const std::string& key, const std::string& value);

  // Add filter_metadata to a cluster with the given name
  void addClusterFilterMetadata(absl::string_view metadata_yaml,
                                absl::string_view cluster_name = "cluster_0");

  // Given an HCM with the default config, set the matcher to be a connect matcher and enable
  // CONNECT requests.
  static void setConnectConfig(HttpConnectionManager& hcm, bool terminate_connect);

  void setLocalReply(
      const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
          config);

  // Set new codecs to use for upstream and downstream codecs.
  void setNewCodecs();

private:
  static bool shouldBoost(envoy::config::core::v3::ApiVersion api_version) {
    return api_version == envoy::config::core::v3::ApiVersion::V2;
  }

  static std::string apiVersionStr(envoy::config::core::v3::ApiVersion api_version) {
    return api_version == envoy::config::core::v3::ApiVersion::V2 ? "V2" : "V3";
  }

  // Load the first HCM struct from the first listener into a parsed proto.
  bool loadHttpConnectionManager(HttpConnectionManager& hcm);
  // Take the contents of the provided HCM proto and stuff them into the first HCM
  // struct of the first listener.
  void storeHttpConnectionManager(const HttpConnectionManager& hcm);

  // Finds the filter named 'name' from the first filter chain from the first listener.
  envoy::config::listener::v3::Filter* getFilterFromListener(const std::string& name);

  // Configure a tap transport socket for a cluster/filter chain.
  void setTapTransportSocket(const std::string& tap_path, const std::string& type,
                             envoy::config::core::v3::TransportSocket& transport_socket,
                             const Protobuf::Message* tls_config);

  // The bootstrap proto Envoy will start up with.
  envoy::config::bootstrap::v3::Bootstrap bootstrap_;

  // The config modifiers added via addConfigModifier() which will be applied in finalize()
  std::vector<ConfigModifierFunction> config_modifiers_;

  // Track if the connect timeout has been set (to avoid clobbering a custom setting with the
  // default).
  bool connect_timeout_set_{false};

  // Option to disable port usage validation for cases where the number of
  // upstream ports created is expected to be larger than the number of
  // upstreams in the config.
  bool skip_port_usage_validation_{false};

  // A sanity check guard to make sure config is not modified after handing it to Envoy.
  bool finalized_{false};
};

class CdsHelper {
public:
  CdsHelper();

  // Set CDS contents on filesystem.
  void setCds(const std::vector<envoy::config::cluster::v3::Cluster>& cluster);
  const std::string& cds_path() const { return cds_path_; }

private:
  const std::string cds_path_;
  uint32_t cds_version_{};
};

// Common code for tests that deliver EDS update via the filesystem.
class EdsHelper {
public:
  EdsHelper();

  // Set EDS contents on filesystem and wait for Envoy to pick this up.
  void setEds(const std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment>&
                  cluster_load_assignments);
  void setEdsAndWait(const std::vector<envoy::config::endpoint::v3::ClusterLoadAssignment>&
                         cluster_load_assignments,
                     IntegrationTestServerStats& server_stats);
  const std::string& eds_path() const { return eds_path_; }

private:
  const std::string eds_path_;
  uint32_t eds_version_{};
  uint32_t update_successes_{};
};

} // namespace Envoy
