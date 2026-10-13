#include <string>

#include "envoy/extensions/filters/listener/local_ratelimit/v3/local_ratelimit.pb.h"
#include "envoy/extensions/filters/listener/local_ratelimit/v3/local_ratelimit.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/listener/local_ratelimit/local_ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace LocalRateLimit {

/**
 * Config registration for the Local Rate Limit filter. @see NamedNetworkFilterConfigFactory.
 */
class LocalRateLimitConfigFactory : public Server::Configuration::NamedListenerFilterConfigFactory {
public:
  // NamedListenerFilterConfigFactory
  Network::ListenerFilterFactoryCb createListenerFilterFactoryFromProto(
      const Protobuf::Message& message,
      const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
      Server::Configuration::ListenerFactoryContext& context) override {

    // Downcast it to the LocalRateLimit config
    const auto& proto_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit&>(
        message, context.messageValidationVisitor());

    FilterConfigSharedPtr config = std::make_shared<FilterConfig>(
        proto_config, context.serverFactoryContext().mainThreadDispatcher(), context.scope(),
        context.serverFactoryContext().runtime());
    return
        [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
          filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
        };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit>();
  }

  std::string name() const override { return "envoy.filters.listener.local_ratelimit"; }
};

/**
 * Static registration for the Local Rate Limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(LocalRateLimitConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory);

} // namespace LocalRateLimit
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
