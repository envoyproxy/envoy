#include "source/extensions/filters/listener/set_filter_state/config.h"

#include "envoy/extensions/filters/listener/set_filter_state/v3/set_filter_state.pb.h"
#include "envoy/extensions/filters/listener/set_filter_state/v3/set_filter_state.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace SetFilterState {

Network::FilterStatus SetFilterState::onAccept(Network::ListenerFilterCallbacks& cb) {
  if (on_accept_ != nullptr) {
    on_accept_->updateFilterState({}, cb.streamInfo());
  }
  return Network::FilterStatus::Continue;
}

/**
 * Config registration for the filter. @see NamedListenerFilterConfigFactory.
 */
class SetFilterStateConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::set_filter_state::v3::Config&>(
        message, context.messageValidationVisitor());

    Filters::Common::SetFilterState::ConfigSharedPtr on_accept_config;
    if (!proto_config.on_accept().empty()) {
      on_accept_config = std::make_shared<Filters::Common::SetFilterState::Config>(
          proto_config.on_accept(), StreamInfo::FilterState::LifeSpan::Connection, context);
    }

    return [listener_filter_matcher,
            on_accept_config](Network::ListenerFilterManager& filter_manager) -> void {
      filter_manager.addAcceptFilter(listener_filter_matcher,
                                     std::make_unique<SetFilterState>(on_accept_config));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::listener::set_filter_state::v3::Config>();
  }

  std::string name() const override { return "envoy.filters.listener.set_filter_state"; }
};

/**
 * Static registration for the filter. @see RegisterFactory.
 */
REGISTER_FACTORY(SetFilterStateConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace SetFilterState
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
