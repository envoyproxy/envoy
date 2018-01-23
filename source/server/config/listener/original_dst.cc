#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "common/config/well_known_names.h"
#include "common/filter/listener/original_dst.h"

namespace Envoy {
namespace Server {
namespace Configuration {

/**
 * Config registration for the original dst filter. @see NamedNetworkFilterConfigFactory.
 */
class OriginalDstConfigFactory : public NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  ListenerFilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                       FactoryContext&) override {
    return [](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(std::make_unique<Filter::Listener::OriginalDst>());
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Envoy::ProtobufWkt::Empty>();
  }

  std::string name() override { return Config::ListenerFilterNames::get().ORIGINAL_DST; }
};

/**
 * Static registration for the original dst filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<OriginalDstConfigFactory, NamedListenerFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
