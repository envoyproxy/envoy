#include "source/extensions/filters/listener/original_src/original_src_config_factory.h"

#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.h"
#include "envoy/extensions/filters/listener/original_src/v3/original_src.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/listener/original_src/config.h"
#include "source/extensions/filters/listener/original_src/original_src.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalSrc {

Network::ListenerFilterFactoryCb OriginalSrcConfigFactory::createListenerFilterFactoryFromProto(
    const Protobuf::Message& message,
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    Server::Configuration::ListenerFactoryContext& context) {
  auto proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::listener::original_src::v3::OriginalSrc&>(
      message, context.messageValidationVisitor());
  Config config(proto_config);
  return [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(listener_filter_matcher,
                                   std::make_unique<OriginalSrcFilter>(config));
  };
}

ProtobufTypes::MessagePtr OriginalSrcConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::filters::listener::original_src::v3::OriginalSrc>();
}
/**
 * Static registration for the original_src filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalSrcConfigFactory, Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.original_src"};

} // namespace OriginalSrc
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
