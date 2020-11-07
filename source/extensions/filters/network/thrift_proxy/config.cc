#include "extensions/filters/network/thrift_proxy/config.h"

#include <map>
#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/filters/network/thrift_proxy/auto_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/auto_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/compact_protocol_impl.h"
#include "extensions/filters/network/thrift_proxy/decoder.h"
#include "extensions/filters/network/thrift_proxy/filters/filter_config.h"
#include "extensions/filters/network/thrift_proxy/filters/well_known_names.h"
#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/stats.h"
#include "extensions/filters/network/thrift_proxy/unframed_transport_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace {

using TransportTypeMap =
    std::map<envoy::extensions::filters::network::thrift_proxy::v3::TransportType, TransportType>;

static const TransportTypeMap& transportTypeMap() {
  CONSTRUCT_ON_FIRST_USE(
      TransportTypeMap,
      {
          {envoy::extensions::filters::network::thrift_proxy::v3::AUTO_TRANSPORT,
           TransportType::Auto},
          {envoy::extensions::filters::network::thrift_proxy::v3::FRAMED, TransportType::Framed},
          {envoy::extensions::filters::network::thrift_proxy::v3::UNFRAMED,
           TransportType::Unframed},
          {envoy::extensions::filters::network::thrift_proxy::v3::HEADER, TransportType::Header},
      });
}

using ProtocolTypeMap =
    std::map<envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType, ProtocolType>;

static const ProtocolTypeMap& protocolTypeMap() {
  CONSTRUCT_ON_FIRST_USE(
      ProtocolTypeMap,
      {
          {envoy::extensions::filters::network::thrift_proxy::v3::AUTO_PROTOCOL,
           ProtocolType::Auto},
          {envoy::extensions::filters::network::thrift_proxy::v3::BINARY, ProtocolType::Binary},
          {envoy::extensions::filters::network::thrift_proxy::v3::LAX_BINARY,
           ProtocolType::LaxBinary},
          {envoy::extensions::filters::network::thrift_proxy::v3::COMPACT, ProtocolType::Compact},
          {envoy::extensions::filters::network::thrift_proxy::v3::TWITTER, ProtocolType::Twitter},
      });
}

TransportType
lookupTransport(envoy::extensions::filters::network::thrift_proxy::v3::TransportType transport) {
  const auto& transport_iter = transportTypeMap().find(transport);
  if (transport_iter == transportTypeMap().end()) {
    throw EnvoyException(fmt::format(
        "unknown transport {}",
        envoy::extensions::filters::network::thrift_proxy::v3::TransportType_Name(transport)));
  }

  return transport_iter->second;
}

ProtocolType
lookupProtocol(envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType protocol) {
  const auto& protocol_iter = protocolTypeMap().find(protocol);
  if (protocol_iter == protocolTypeMap().end()) {
    throw EnvoyException(fmt::format(
        "unknown protocol {}",
        envoy::extensions::filters::network::thrift_proxy::v3::ProtocolType_Name(protocol)));
  }
  return protocol_iter->second;
}

} // namespace

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProtocolOptions& config)
    : transport_(lookupTransport(config.transport())),
      protocol_(lookupProtocol(config.protocol())) {}

TransportType ProtocolOptionsConfigImpl::transport(TransportType downstream_transport) const {
  return (transport_ == TransportType::Auto) ? downstream_transport : transport_;
}

ProtocolType ProtocolOptionsConfigImpl::protocol(ProtocolType downstream_protocol) const {
  return (protocol_ == ProtocolType::Auto) ? downstream_protocol : protocol_;
}

Network::FilterFactoryCb ThriftProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Config> filter_config(new ConfigImpl(proto_config, context));

  return [filter_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<ConnectionManager>(
        *filter_config, context.api().randomGenerator(), context.dispatcher().timeSource()));
  };
}

/**
 * Static registration for the thrift filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ThriftProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

ConfigImpl::ConfigImpl(
    const envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config,
    Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("thrift.{}.", config.stat_prefix())),
      stats_(ThriftFilterStats::generateStats(stats_prefix_, context_.scope())),
      transport_(lookupTransport(config.transport())), proto_(lookupProtocol(config.protocol())),
      route_matcher_(new Router::RouteMatcher(config.route_config())) {

  if (config.thrift_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::extensions::filters::network::thrift_proxy::v3::ThriftFilter router;
    router.set_name(ThriftFilters::ThriftFilterNames::get().ROUTER);
    processFilter(router);
  } else {
    for (const auto& filter : config.thrift_filters()) {
      processFilter(filter);
    }
  }
}

void ConfigImpl::createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const ThriftFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

TransportPtr ConfigImpl::createTransport() {
  return NamedTransportConfigFactory::getFactory(transport_).createTransport();
}

ProtocolPtr ConfigImpl::createProtocol() {
  return NamedProtocolConfigFactory::getFactory(proto_).createProtocol();
}

void ConfigImpl::processFilter(
    const envoy::extensions::filters::network::thrift_proxy::v3::ThriftFilter& proto_config) {
  const std::string& string_name = proto_config.name();

  ENVOY_LOG(debug, "    thrift filter #{}", filter_factories_.size());
  ENVOY_LOG(debug, "      name: {}", string_name);
  ENVOY_LOG(debug, "    config: {}",
            MessageUtil::getJsonStringFromMessage(
                proto_config.has_typed_config()
                    ? static_cast<const Protobuf::Message&>(proto_config.typed_config())
                    : static_cast<const Protobuf::Message&>(
                          proto_config.hidden_envoy_deprecated_config()),
                true));
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<ThriftFilters::NamedThriftFilterConfigFactory>(
          proto_config);

  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
      proto_config, context_.messageValidationVisitor(), factory);
  ThriftFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
