#include "extension_registry.h"

#include "source/common/network/socket_interface_impl.h"
#include "source/common/upstream/logical_dns_cluster.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "source/extensions/compression/brotli/decompressor/config.h"
#include "source/extensions/compression/gzip/decompressor/config.h"
#include "source/extensions/filters/http/buffer/config.h"
#include "source/extensions/filters/http/decompressor/config.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"
#include "source/extensions/filters/http/router/config.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"
#include "source/extensions/http/original_ip_detection/xff/config.h"
#include "source/extensions/stat_sinks/metrics_service/config.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/config.h"
#include "source/extensions/upstreams/http/generic/config.h"

#include "extension_registry_platform_additions.h"
#include "library/common/extensions/filters/http/assertion/config.h"
#include "library/common/extensions/filters/http/local_error/config.h"
#include "library/common/extensions/filters/http/network_configuration/config.h"
#include "library/common/extensions/filters/http/platform_bridge/config.h"
#include "library/common/extensions/filters/http/route_cache_reset/config.h"
#include "library/common/extensions/retry/options/network_configuration/config.h"

namespace Envoy {

void ExtensionRegistry::registerFactories() {
  Envoy::Extensions::Clusters::DynamicForwardProxy::forceRegisterClusterFactory();
  Envoy::Extensions::Compression::Brotli::Decompressor::
      forceRegisterBrotliDecompressorLibraryFactory();
  Envoy::Extensions::Compression::Gzip::Decompressor::forceRegisterGzipDecompressorLibraryFactory();
  Envoy::Extensions::Http::OriginalIPDetection::Xff::forceRegisterXffIPDetectionFactory();
  Envoy::Extensions::HttpFilters::Assertion::forceRegisterAssertionFilterFactory();
  Envoy::Extensions::HttpFilters::Decompressor::forceRegisterDecompressorFilterFactory();
  Envoy::Extensions::HttpFilters::BufferFilter::forceRegisterBufferFilterFactory();
  Envoy::Extensions::HttpFilters::DynamicForwardProxy::
      forceRegisterDynamicForwardProxyFilterFactory();
  Envoy::Extensions::HttpFilters::LocalError::forceRegisterLocalErrorFilterFactory();
  Envoy::Extensions::HttpFilters::PlatformBridge::forceRegisterPlatformBridgeFilterFactory();
  Envoy::Extensions::HttpFilters::RouteCacheReset::forceRegisterRouteCacheResetFilterFactory();
  Envoy::Extensions::HttpFilters::RouterFilter::forceRegisterRouterFilterConfig();
  Envoy::Extensions::HttpFilters::NetworkConfiguration::
      forceRegisterNetworkConfigurationFilterFactory();
  Envoy::Extensions::NetworkFilters::HttpConnectionManager::
      forceRegisterHttpConnectionManagerFilterConfigFactory();
  Envoy::Extensions::Retry::Options::
      forceRegisterNetworkConfigurationRetryOptionsPredicateFactory();
  Envoy::Extensions::StatSinks::MetricsService::forceRegisterMetricsServiceSinkFactory();
  Envoy::Extensions::TransportSockets::RawBuffer::forceRegisterUpstreamRawBufferSocketFactory();
  Envoy::Extensions::TransportSockets::Tls::forceRegisterUpstreamSslSocketFactory();
  Envoy::Extensions::TransportSockets::Tls::forceRegisterDefaultCertValidatorFactory();
  Envoy::Extensions::Upstreams::Http::Generic::forceRegisterGenericGenericConnPoolFactory();
  Envoy::Upstream::forceRegisterLogicalDnsClusterFactory();
  ExtensionRegistryPlatformAdditions::registerFactories();

  // TODO: add a "force initialize" function to the upstream code, or clean up the upstream code
  // in such a way that does not depend on the statically initialized variable.
  // The current setup exposes in iOS the same problem as the one described in:
  // https://github.com/envoyproxy/envoy/pull/7185 with the static variable declared in:
  // https://github.com/envoyproxy/envoy/pull/11380/files#diff-8a5c90e5a39b2ea975170edc4434345bR138.
  // For now force the compilation unit to run by creating an instance of the class declared in
  // socket_interface_impl.h and immediately destroy.
  auto ptr = std::make_unique<Network::SocketInterfaceImpl>();
  ptr.reset(nullptr);
}

} // namespace Envoy
