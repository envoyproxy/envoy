#include "source/extensions/filters/http/priority_load_shed/config.h"

#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/priority_load_shed/filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {

absl::StatusOr<Http::FilterFactoryCb>
PriorityLoadShedFilterFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::priority_load_shed::v3::PriorityLoadShed& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();
  Server::OverloadManager& overload_manager = context.listenerInfo().shouldBypassOverloadManager()
                                                  ? server_context.nullOverloadManager()
                                                  : server_context.overloadManager();

  auto filter_config = PriorityLoadShedFilterConfig::create(proto_config, overload_manager,
                                                            stats_prefix, context.scope());
  if (!filter_config.ok()) {
    return filter_config.status();
  }

  return [filter_config = std::move(filter_config.value())](
             Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<PriorityLoadShedFilter>(filter_config));
  };
}

REGISTER_FACTORY(PriorityLoadShedFilterFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
