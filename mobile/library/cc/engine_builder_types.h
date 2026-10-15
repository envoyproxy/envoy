#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/base.pb.h"

namespace Envoy {
namespace Platform {

// Represents the locality information in the Bootstrap's node, as defined in:
// https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-locality
struct NodeLocality {
  std::string region;
  std::string zone;
  std::string sub_zone;
};

#ifdef ENVOY_MOBILE_XDS
constexpr int DefaultXdsTimeout = 5;

// Forward declarations so they can be friended by XdsBuilder.
class EngineBuilder;
class MobileEngineBuilder;

// A class for building the xDS configuration for the Envoy Mobile engine.
// xDS is a protocol for dynamic configuration of Envoy instances, more information can be found in:
// https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol.
//
// This class is typically used as input to the EngineBuilder's/MobileEngineBuilder's setXds()
// method.
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
  // Required so that both Builders can call the XdsBuilder's protected build() method.
  friend class EngineBuilder;
  friend class MobileEngineBuilder;

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
#endif // ENVOY_MOBILE_XDS

} // namespace Platform
} // namespace Envoy
