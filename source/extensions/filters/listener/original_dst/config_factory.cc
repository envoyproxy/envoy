#include "extensions/filters/listener/original_dst/config_factory.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/listener/original_dst/original_dst.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace OriginalDst {

Network::ListenerFilterFactoryCb OriginalDstConfigFactory::createListenerFilterFactoryFromProto(
    const Protobuf::Message& message,
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    Server::Configuration::ListenerFactoryContext& context) {

  auto proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::listener::original_dst::v3::OriginalDst&>(
      message, context.messageValidationVisitor());

#ifdef WIN32
  // On Windows we need to do some extra validation for the Original Destination filter.
  // In particular we need to check if:
  // 1. The platform supports the original destination feature
  // 2. The `traffic_direction` property is set on the listener. This is required to redirect the
  // traffic.
  if (context.listenerConfig().direction() == envoy::config::core::v3::UNSPECIFIED) {
    throw EnvoyException("[Windows] Setting original destination filter on a listener without "
                         "specifying the traffic_direction."
                         "Configure the traffic_direction listener option");
  }
  if (!Platform::win32SupportsOriginalDestination()) {
    throw EnvoyException("[Windows] Envoy was compiled without support for `SO_ORIGINAL_DST`, "
                         "the original destination filter cannot be used");
  }
#endif

  Config config(proto_config, context.listenerConfig().direction());
  return [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(listener_filter_matcher,
                                   std::make_unique<OriginalDstFilter>(config));
  };
}

/**
 * Static registration for the original_src filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalDstConfigFactory, Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.original_dst"};

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy