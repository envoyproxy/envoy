#include <string>

#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.h"
#include "envoy/extensions/filters/listener/original_dst/v3/original_dst.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/network/filter_state_dst_address.h"
#include "source/extensions/filters/listener/original_dst/original_dst.h"

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
      Server::Configuration::ListenerFactoryContext& context) override {
#ifdef WIN32
    // On Windows we need to do some extra validation for the Original Destination filter.
    // In particular we need to check if:
    // 1. The platform supports the original destination feature
    // 2. The `traffic_direction` property is set on the listener. This is required to redirect the
    // traffic.
    if (context.listenerInfo().direction() == envoy::config::core::v3::UNSPECIFIED) {
      throw EnvoyException("[Windows] Setting original destination filter on a listener without "
                           "specifying the traffic_direction."
                           "Configure the traffic_direction listener option");
    }
    if (!Platform::win32SupportsOriginalDestination()) {
      throw EnvoyException("[Windows] Envoy was compiled without support for `SO_ORIGINAL_DST`, "
                           "the original destination filter cannot be used");
    }
#endif

    return [listener_filter_matcher, traffic_direction = context.listenerInfo().direction()](
               Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<OriginalDstFilter>(traffic_direction));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::listener::original_dst::v3::OriginalDst>();
  }

  std::string name() const override { return FilterNames::get().Name; }
};

/**
 * Static registration for the original dst filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OriginalDstConfigFactory, Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.original_dst"};

class OriginalDstLocalFilterStateFactory : public Network::BaseAddressObjectFactory {
public:
  std::string name() const override { return FilterNames::get().LocalFilterStateKey; }
};

REGISTER_FACTORY(OriginalDstLocalFilterStateFactory, StreamInfo::FilterState::ObjectFactory);

class OriginalDstRemoteFilterStateFactory : public Network::BaseAddressObjectFactory {
public:
  std::string name() const override { return FilterNames::get().RemoteFilterStateKey; }
};

REGISTER_FACTORY(OriginalDstRemoteFilterStateFactory, StreamInfo::FilterState::ObjectFactory);

} // namespace OriginalDst
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
