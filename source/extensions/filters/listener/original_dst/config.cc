#include <string>

#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "extensions/filters/listener/original_dst/original_dst.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
class OriginalDstConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message&,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext&) override {
    return [listener_filter_matcher](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<OriginalDstFilter>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::listener::original_dst::v3::OriginalDst>();
  }

  std::string name() const override { return ListenerFilterNames::get().OriginalDst; }
};

/**
 * Static registration for the original dst filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalDstConfigFactory, Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.original_dst"};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
