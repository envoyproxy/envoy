#include "extensions/filters/network/sip_proxy/config.h"

#include <map>
#include <string>

#include "envoy/extensions/filters/network/sip_proxy/v3/sip_proxy.pb.h"
#include "envoy/extensions/filters/network/sip_proxy/v3/sip_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/utility.h"

#include "extensions/filters/network/sip_proxy/decoder.h"
#include "extensions/filters/network/sip_proxy/filters/filter_config.h"
#include "extensions/filters/network/sip_proxy/filters/well_known_names.h"
#include "extensions/filters/network/sip_proxy/router/router_impl.h"
#include "extensions/filters/network/sip_proxy/stats.h"
//#include "extensions/filters/network/sip_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

namespace {
inline void
addUniqueClusters(absl::flat_hash_set<std::string>& clusters,
                  const envoy::extensions::filters::network::sip_proxy::v3::Route& route) {
  clusters.emplace(route.route().cluster());
}
} // namespace

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::filters::network::sip_proxy::v3::SipProtocolOptions& config) {
  UNREFERENCED_PARAMETER(config);
}

Network::FilterFactoryCb SipProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::sip_proxy::v3::SipProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<Config> filter_config(new ConfigImpl(proto_config, context));

  absl::flat_hash_set<std::string> unique_clusters;
  for (auto& route : proto_config.route_config().routes()) {
    addUniqueClusters(unique_clusters, route);
  }

  /**
   * ConnPool::InstanceImpl contains ThreadLocalObject ThreadLocalPool which only can be
   * instantianced on main thread. so construct ConnPool::InstanceImpl here.
   */
  auto transaction_infos = std::make_shared<Router::TransactionInfos>();
  for (auto& cluster : unique_clusters) {
    Stats::ScopePtr stats_scope =
        context.scope().createScope(fmt::format("cluster.{}.sip_cluster", cluster));
    auto transaction_info_ptr =
        std::make_shared<Router::TransactionInfo>(cluster, context.threadLocal(),
          static_cast<std::chrono::milliseconds>(PROTOBUF_GET_MS_OR_DEFAULT(proto_config.settings(), transaction_timeout, 32000))
            );
    transaction_info_ptr->init();
    transaction_infos->emplace(cluster, transaction_info_ptr);
  }

  return [filter_config, &context,
          transaction_infos](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(
        std::make_shared<ConnectionManager>(*filter_config, context.api().randomGenerator(),
                                            context.dispatcher().timeSource(), transaction_infos));
  };
}

/**
 * Static registration for the sip filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SipProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

ConfigImpl::ConfigImpl(const envoy::extensions::filters::network::sip_proxy::v3::SipProxy& config,
                       Server::Configuration::FactoryContext& context)
    : context_(context), stats_prefix_(fmt::format("sip.{}.", config.stat_prefix())),
      stats_(SipFilterStats::generateStats(stats_prefix_, context_.scope())),
      route_matcher_(new Router::RouteMatcher(config.route_config())),
      settings_(std::make_shared<SipSettings>(
          static_cast<std::chrono::milliseconds>(PROTOBUF_GET_MS_REQUIRED(config.settings(), transaction_timeout)),
          config.settings().session_stickness())) {

  if (config.sip_filters().empty()) {
    ENVOY_LOG(debug, "using default router filter");

    envoy::extensions::filters::network::sip_proxy::v3::SipFilter router;
    router.set_name(SipFilters::SipFilterNames::get().ROUTER);
    processFilter(router);
  } else {
    for (const auto& filter : config.sip_filters()) {
      processFilter(filter);
    }
  }
}

void ConfigImpl::createFilterChain(SipFilters::FilterChainFactoryCallbacks& callbacks) {
  for (const SipFilters::FilterFactoryCb& factory : filter_factories_) {
    factory(callbacks);
  }
}

void ConfigImpl::processFilter(
    const envoy::extensions::filters::network::sip_proxy::v3::SipFilter& proto_config) {
  const std::string& string_name = proto_config.name();

  ENVOY_LOG(debug, "    sip filter #{}", filter_factories_.size());
  ENVOY_LOG(debug, "      name: {}", string_name);
  ENVOY_LOG(debug, "    config: {}",
            MessageUtil::getJsonStringFromMessageOrError(
                // niefei
                // proto_config.has_typed_config()
                //    ? static_cast<const Protobuf::Message&>(proto_config.typed_config())
                //    : static_cast<const Protobuf::Message&>(
                //          proto_config.hidden_envoy_deprecated_config()),
                static_cast<const Protobuf::Message&>(proto_config.typed_config())));
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactory<SipFilters::NamedSipFilterConfigFactory>(
          proto_config);

  //  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateToFactoryConfig(
  ProtobufTypes::MessagePtr message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      proto_config.typed_config(), context_.messageValidationVisitor(), factory);
  SipFilters::FilterFactoryCb callback =
      factory.createFilterFactoryFromProto(*message, stats_prefix_, context_);

  filter_factories_.push_back(callback);
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
