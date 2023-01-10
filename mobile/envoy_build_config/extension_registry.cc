#include "extension_registry.h"

#include "source/common/http/match_delegate/config.h"
#include "source/common/http/matching/inputs.h"
#include "source/common/network/default_client_connection_factory.h"
#include "source/common/network/matching/inputs.h"
#include "source/common/network/socket_interface_impl.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/watchdog/abort_action_config.h"
#include "source/extensions/clusters/dynamic_forward_proxy/cluster.h"
#include "source/extensions/clusters/logical_dns/logical_dns_cluster.h"
#include "source/extensions/clusters/static/static_cluster.h"
#include "source/extensions/compression/brotli/decompressor/config.h"
#include "source/extensions/compression/gzip/decompressor/config.h"
#include "source/extensions/early_data/default_early_data_policy.h"
#include "source/extensions/filters/http/alternate_protocols_cache/config.h"
#include "source/extensions/filters/http/buffer/config.h"
#include "source/extensions/filters/http/decompressor/config.h"
#include "source/extensions/filters/http/dynamic_forward_proxy/config.h"
#include "source/extensions/filters/http/router/config.h"
#include "source/extensions/filters/network/http_connection_manager/config.h"
#include "source/extensions/http/header_formatters/preserve_case/config.h"
#include "source/extensions/http/header_validators/envoy_default/config.h"
#include "source/extensions/http/original_ip_detection/xff/config.h"
#include "source/extensions/listener_managers/listener_manager/connection_handler_impl.h"
#include "source/extensions/listener_managers/listener_manager/listener_manager_impl.h"
#include "source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.h"
#include "source/extensions/path/match/uri_template/config.h"
#include "source/extensions/path/rewrite/uri_template/config.h"
#include "source/extensions/request_id/uuid/config.h"
#include "source/extensions/stat_sinks/metrics_service/config.h"
#include "source/extensions/transport_sockets/http_11_proxy/config.h"
#include "source/extensions/transport_sockets/raw_buffer/config.h"
#include "source/extensions/transport_sockets/tls/cert_validator/default_validator.h"
#include "source/extensions/transport_sockets/tls/config.h"
#include "source/extensions/udp_packet_writer/default/config.h"
#include "source/extensions/upstreams/http/generic/config.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/quic_transport_socket_factory.h"
#include "source/common/quic/server_codec_impl.h"
#include "source/extensions/quic/connection_id_generator/envoy_deterministic_connection_id_generator_config.h"
#include "source/extensions/quic/crypto_stream/envoy_quic_crypto_server_stream.h"
#include "source/extensions/quic/proof_source/envoy_quic_proof_source_factory_impl.h"
#endif

#include "extension_registry_platform_additions.h"
#include "library/common/extensions/cert_validator/platform_bridge/config.h"
#include "library/common/extensions/filters/http/assertion/config.h"
#include "library/common/extensions/filters/http/local_error/config.h"
#include "library/common/extensions/filters/http/network_configuration/config.h"
#include "library/common/extensions/filters/http/platform_bridge/config.h"
#include "library/common/extensions/filters/http/route_cache_reset/config.h"
#include "library/common/extensions/filters/http/socket_tag/config.h"
#include "library/common/extensions/filters/http/test_accessor/config.h"
#include "library/common/extensions/filters/http/test_event_tracker/config.h"
#include "library/common/extensions/filters/http/test_kv_store/config.h"
#include "library/common/extensions/filters/http/test_logger/config.h"
#include "library/common/extensions/filters/http/test_read/config.h"
#include "library/common/extensions/key_value/platform/config.h"
#include "library/common/extensions/listener_managers/api_listener_manager/api_listener_manager.h"
#include "library/common/extensions/retry/options/network_configuration/config.h"

namespace Envoy {

void ExtensionRegistry::registerFactories() {
  Common::Http::MatchDelegate::Factory::forceRegisterSkipActionFactory();
  Common::Http::MatchDelegate::forceRegisterMatchDelegateConfig();
  ExtensionRegistryPlatformAdditions::registerFactories();
  Extensions::Clusters::DynamicForwardProxy::forceRegisterClusterFactory();
  Extensions::Compression::Brotli::Decompressor::forceRegisterBrotliDecompressorLibraryFactory();
  Extensions::Compression::Gzip::Decompressor::forceRegisterGzipDecompressorLibraryFactory();
  Extensions::Http::HeaderFormatters::PreserveCase::
      forceRegisterPreserveCaseFormatterFactoryConfig();
  Extensions::Http::HeaderValidators::EnvoyDefault::forceRegisterHeaderValidatorFactoryConfig();
  Extensions::Http::OriginalIPDetection::Xff::forceRegisterXffIPDetectionFactory();
  Extensions::HttpFilters::AlternateProtocolsCache::
      forceRegisterAlternateProtocolsCacheFilterFactory();
  Extensions::HttpFilters::Assertion::forceRegisterAssertionFilterFactory();
  Extensions::HttpFilters::BufferFilter::forceRegisterBufferFilterFactory();
  Extensions::HttpFilters::Decompressor::forceRegisterDecompressorFilterFactory();
  Extensions::HttpFilters::DynamicForwardProxy::forceRegisterDynamicForwardProxyFilterFactory();
  Extensions::HttpFilters::LocalError::forceRegisterLocalErrorFilterFactory();
  Extensions::HttpFilters::NetworkConfiguration::forceRegisterNetworkConfigurationFilterFactory();
  Extensions::HttpFilters::PlatformBridge::forceRegisterPlatformBridgeFilterFactory();
  Extensions::HttpFilters::RouteCacheReset::forceRegisterRouteCacheResetFilterFactory();
  Extensions::HttpFilters::RouterFilter::forceRegisterRouterFilterConfig();
  Extensions::HttpFilters::SocketTag::forceRegisterSocketTagFilterFactory();
  Extensions::HttpFilters::TestAccessor::forceRegisterTestAccessorFilterFactory();
  Extensions::HttpFilters::TestEventTracker::forceRegisterTestEventTrackerFilterFactory();
  Extensions::HttpFilters::TestKeyValueStore::forceRegisterTestKeyValueStoreFilterFactory();
  Extensions::HttpFilters::TestLogger::forceRegisterFactory();
  Extensions::KeyValue::forceRegisterPlatformKeyValueStoreFactory();
  Extensions::NetworkFilters::HttpConnectionManager::
      forceRegisterHttpConnectionManagerFilterConfigFactory();
  Extensions::RequestId::forceRegisterUUIDRequestIDExtensionFactory();
  Extensions::Retry::Options::forceRegisterNetworkConfigurationRetryOptionsPredicateFactory();
  Extensions::StatSinks::MetricsService::forceRegisterMetricsServiceSinkFactory();
  Extensions::TransportSockets::Http11Connect::
      forceRegisterUpstreamHttp11ConnectSocketConfigFactory();
  Extensions::TransportSockets::RawBuffer::forceRegisterDownstreamRawBufferSocketFactory();
  Extensions::TransportSockets::RawBuffer::forceRegisterUpstreamRawBufferSocketFactory();
  Extensions::TransportSockets::Tls::forceRegisterDefaultCertValidatorFactory();
  Extensions::TransportSockets::Tls::forceRegisterPlatformBridgeCertValidatorFactory();
  Extensions::TransportSockets::Tls::forceRegisterUpstreamSslSocketFactory();
  Extensions::Upstreams::Http::forceRegisterProtocolOptionsConfigFactory();
  Extensions::Upstreams::Http::Generic::forceRegisterGenericGenericConnPoolFactory();
  Extensions::UriTemplate::Match::forceRegisterUriTemplateMatcherFactory();
  Extensions::UriTemplate::Rewrite::forceRegisterUriTemplateRewriterFactory();
  Http::Matching::forceRegisterHttpRequestHeadersDataInputFactory();
  Http::Matching::forceRegisterHttpRequestTrailersDataInputFactory();
  Http::Matching::forceRegisterHttpResponseHeadersDataInputFactory();
  Http::Matching::forceRegisterHttpResponseTrailersDataInputFactory();
  HttpFilters::TestRead::forceRegisterTestReadFilterFactory();
  Network::Address::forceRegisterIpResolver();
  Network::forceRegisterDefaultClientConnectionFactory();
  Network::forceRegisterGetAddrInfoDnsResolverFactory();
  Network::forceRegisterSocketInterfaceImpl();
  Network::forceRegisterUdpDefaultWriterFactoryFactory();
  Network::Matching::forceRegisterApplicationProtocolInputFactory();
  Network::Matching::forceRegisterDestinationPortInputFactory();
  Network::Matching::forceRegisterDirectSourceIPInputFactory();
  Network::Matching::forceRegisterHttpDestinationIPInputFactory();
  Network::Matching::forceRegisterHttpDestinationPortInputFactory();
  Network::Matching::forceRegisterHttpDirectSourceIPInputFactory();
  Network::Matching::forceRegisterHttpServerNameInputFactory();
  Network::Matching::forceRegisterHttpSourceIPInputFactory();
  Network::Matching::forceRegisterHttpSourcePortInputFactory();
  Network::Matching::forceRegisterHttpSourceTypeInputFactory();
  Network::Matching::forceRegisterServerNameInputFactory();
  Network::Matching::forceRegisterSourceIPInputFactory();
  Network::Matching::forceRegisterSourcePortInputFactory();
  Network::Matching::forceRegisterSourceTypeInputFactory();
  Network::Matching::forceRegisterTransportProtocolInputFactory();
  Network::Matching::forceRegisterUdpDestinationIPInputFactory();
  Network::Matching::forceRegisterUdpDestinationPortInputFactory();
  Network::Matching::forceRegisterUdpSourceIPInputFactory();
  Network::Matching::forceRegisterUdpSourcePortInputFactory();
  Regex::forceRegisterGoogleReEngineFactory();
  Router::forceRegisterDefaultEarlyDataPolicyFactory();
  Router::forceRegisterRouteListMatchActionFactory();
  Router::forceRegisterRouteMatchActionFactory();
  Router::forceRegisterUpstreamCodecFilterFactory();
  Server::FilterChain::forceRegisterFilterChainNameActionFactory();
  Server::forceRegisterApiListenerManagerFactoryImpl();
  Server::forceRegisterConnectionHandlerFactoryImpl();
  Server::forceRegisterDefaultListenerManagerFactoryImpl();
  Upstream::forceRegisterLogicalDnsClusterFactory();
  Upstream::forceRegisterStaticClusterFactory();
  Watchdog::forceRegisterAbortActionFactory();

#ifdef ENVOY_ENABLE_QUIC
  Quic::forceRegisterEnvoyDeterministicConnectionIdGeneratorConfigFactory();
  Quic::forceRegisterEnvoyQuicCryptoServerStreamFactoryImpl();
  Quic::forceRegisterEnvoyQuicProofSourceFactoryImpl();
  Quic::forceRegisterQuicClientTransportSocketConfigFactory();
  Quic::forceRegisterQuicHttpServerConnectionFactoryImpl();
  Quic::forceRegisterQuicServerTransportSocketConfigFactory();
#endif
}

} // namespace Envoy
