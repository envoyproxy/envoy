#include "library/common/extensions/registry.h"

namespace Envoy {

void ExtensionRegistry::registerFactories() {
  Envoy::Extensions::Clusters::DynamicForwardProxy::forceRegisterClusterFactory();
  Envoy::Extensions::Compression::Gzip::Decompressor::forceRegisterGzipDecompressorLibraryFactory();
  Envoy::Extensions::HttpFilters::Decompressor::forceRegisterDecompressorFilterFactory();
  Envoy::Extensions::HttpFilters::DynamicForwardProxy::
      forceRegisterDynamicForwardProxyFilterFactory();
  Envoy::Extensions::HttpFilters::RouterFilter::forceRegisterRouterFilterConfig();
  Envoy::Extensions::NetworkFilters::HttpConnectionManager::
      forceRegisterHttpConnectionManagerFilterConfigFactory();
  Envoy::Extensions::StatSinks::MetricsService::forceRegisterMetricsServiceSinkFactory();
  Envoy::Extensions::TransportSockets::Tls::forceRegisterUpstreamSslSocketFactory();
  Envoy::Upstream::forceRegisterLogicalDnsClusterFactory();
}

} // namespace Envoy
