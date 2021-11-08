#include "extensions/filters/network/brpc_proxy/config.h"
#include "extensions/filters/network/brpc_proxy/client_impl.h"
#include "extensions/filters/network/brpc_proxy/conn_pool_impl.h"
#include "extensions/filters/network/brpc_proxy/codec_impl.h"
#include "extensions/filters/network/brpc_proxy/request_manager_impl.h"
#include "extensions/filters/network/brpc_proxy/proxy_filter.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

Network::FilterFactoryCb BrpcProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
	const envoy::extensions::filters::network::brpc_proxy::v3::BrpcProxy& proto_config,
	Server::Configuration::FactoryContext& context) {
	BrpcProxyStatsSharedPtr stat = generateStats(fmt::format("brpc.{}.", proto_config.stat_prefix()), context.scope());
	RequestManager::Upstreams upstreams;
	auto conn_pool_ptr = std::make_shared<ConnInstanceImpl>(
		proto_config.cluster(), context.clusterManager(),ClientFactoryImpl::instance_,
		context.threadLocal());
	conn_pool_ptr->init();
	upstreams[proto_config.cluster()] = conn_pool_ptr;
	RequestManager::RMInstanceSharedPtr rm = std::make_shared<RequestManager::RMInstanceImpl>(upstreams);
	return [rm, stat](Network::FilterManager& filter_manager) -> void {
    DecoderFactoryImpl factory;
    filter_manager.addReadFilter(std::make_shared<ProxyFilter>(
        factory, EncoderPtr{new EncoderImpl()}, *rm, stat));
  };
}


/**
 * Static registration for the brpc filter. @see RegisterFactory.
 */
REGISTER_FACTORY(BrpcProxyFilterConfigFactory,
				 Server::Configuration::NamedNetworkFilterConfigFactory);


} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy


