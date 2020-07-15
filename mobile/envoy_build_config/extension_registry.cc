#include "extension_registry.h"

#include "common/network/socket_interface_impl.h"

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
  Envoy::Extensions::Upstreams::Http::Generic::forceRegisterGenericGenericConnPoolFactory();
  Envoy::Upstream::forceRegisterLogicalDnsClusterFactory();
  Envoy::Extensions::HttpFilters::PlatformBridge::forceRegisterPlatformBridgeFilterFactory();

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
