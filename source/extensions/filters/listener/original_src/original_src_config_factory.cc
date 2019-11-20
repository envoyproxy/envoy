#include "extensions/filters/listener/original_src/original_src_config_factory.h"

#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.h"
#include "envoy/config/filter/listener/original_src/v2alpha1/original_src.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/listener/original_src/config.h"
#include "extensions/filters/listener/original_src/original_src.h"
#include "extensions/filters/listener/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

Network::ListenerFilterFactoryCb OriginalSrcConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& message, Server::Configuration::ListenerFactoryContext& context) {
  auto proto_config = MessageUtil::downcastAndValidate<
      const envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc&>(
      message, context.messageValidationVisitor());
  Config config(proto_config);
  return [config](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(std::make_unique<OriginalSrcFilter>(config));
  };
}

ProtobufTypes::MessagePtr OriginalSrcConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::config::filter::listener::original_src::v2alpha1::OriginalSrc>();
}
/**
 * Static registration for the original_src filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalSrcConfigFactory, Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
