#include "extensions/filters/network/thrift_proxy/config.h"

#include <map>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/decoder.h"
#include "extensions/filters/network/thrift_proxy/filters/filter_config.h"
#include "extensions/filters/network/thrift_proxy/filters/well_known_names.h"
#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/stats.h"
#include "extensions/filters/network/thrift_proxy/transport_impl.h"
#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

typedef std::map<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType,
                 TransportType>
    TransportTypeMap;

static const TransportTypeMap& transportTypeMap() {
  CONSTRUCT_ON_FIRST_USE(
      TransportTypeMap,
      {
          {envoy::config::filter::network::thrift_proxy::v2alpha1::
               ThriftProxy_TransportType_AUTO_TRANSPORT,
           TransportType::Auto},
          {envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_TransportType_FRAMED,
           TransportType::Framed},
          {envoy::config::filter::network::thrift_proxy::v2alpha1::
               ThriftProxy_TransportType_UNFRAMED,
           TransportType::Unframed},
      });
}

typedef std::map<envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType,
                 ProtocolType>
    ProtocolTypeMap;

static const ProtocolTypeMap& protocolTypeMap() {
  CONSTRUCT_ON_FIRST_USE(
      ProtocolTypeMap,
      {
          {envoy::config::filter::network::thrift_proxy::v2alpha1::
               ThriftProxy_ProtocolType_AUTO_PROTOCOL,
           ProtocolType::Auto},
          {envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType_BINARY,
           ProtocolType::Binary},
          {envoy::config::filter::network::thrift_proxy::v2alpha1::
               ThriftProxy_ProtocolType_LAX_BINARY,
           ProtocolType::LaxBinary},
          {envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy_ProtocolType_COMPACT,
           ProtocolType::Compact},
      });
}

} // namespace

Network::FilterFactoryCb ThriftProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Config> filter_config(new ConfigImpl(proto_config, context));

  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ConnectionManager>(*filter_config));
  };
}

/**
 * Static registration for the thrift filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ThriftProxyFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

ConfigImpl::ConfigImpl(
    const envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy& config,
    Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("thrift.{}.", config.stat_prefix())),
      stats_(ThriftFilterStats::generateStats(stats_prefix_, context_.scope())),
      transport_(config.transport()), proto_(config.protocol()),
      route_matcher_(new Router::RouteMatcher(config.route_config())) {

  // Construct the only Thrift DecoderFilter: the Router
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<ThriftFilters::NamedThriftFilterConfigFactory>(
          ThriftFilters::ThriftFilterNames::get().ROUTER);
  ThriftFilters::FilterFactoryCb callback;

  auto empty_config = factory.createEmptyConfigProto();
  callback = factory.createFilterFactoryFromProto(*empty_config, stats_prefix_, context_);
  filter_factories_.push_back(callback);
}

void ConfigImpl::createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const ThriftFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

DecoderPtr ConfigImpl::createDecoder(DecoderCallbacks& callbacks) {
  return std::make_unique<Decoder>(createTransport(), createProtocol(), callbacks);
}

TransportPtr ConfigImpl::createTransport() {
  TransportTypeMap::const_iterator i = transportTypeMap().find(transport_);
  RELEASE_ASSERT(i != transportTypeMap().end(), "invalid transport type");

  return NamedTransportConfigFactory::getFactory(i->second).createTransport();
}

ProtocolPtr ConfigImpl::createProtocol() {
  ProtocolTypeMap::const_iterator i = protocolTypeMap().find(proto_);
  RELEASE_ASSERT(i != protocolTypeMap().end(), "invalid protocol type");
  return NamedProtocolConfigFactory::getFactory(i->second).createProtocol();
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
