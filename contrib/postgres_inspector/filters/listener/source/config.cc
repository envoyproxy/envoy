#include "contrib/postgres_inspector/filters/listener/source/config.h"

#include <string>

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/protobuf/message_validator_impl.h"

#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.h"
#include "contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha/postgres_inspector.pb.validate.h"
#include "contrib/postgres_inspector/filters/listener/source/postgres_inspector.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace PostgresInspector {

Network::ListenerFilterFactoryCb
PostgresInspectorConfigFactory::createListenerFilterFactoryFromProto(
    const Protobuf::Message& message,
    const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher,
    Server::Configuration::ListenerFactoryContext& context) {

  // Downcast it to the Postgres inspector config.
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector&>(
      message, context.messageValidationVisitor());

  ConfigSharedPtr config = std::make_shared<Config>(context.scope(), proto_config);

  return [listener_filter_matcher, config](Network::ListenerFilterManager& filter_manager) -> void {
    filter_manager.addAcceptFilter(listener_filter_matcher, std::make_unique<Filter>(config));
  };
}

ProtobufTypes::MessagePtr PostgresInspectorConfigFactory::createEmptyConfigProto() {
  return std::make_unique<
      envoy::extensions::filters::listener::postgres_inspector::v3alpha::PostgresInspector>();
}

std::string PostgresInspectorConfigFactory::name() const {
  return "envoy.filters.listener.postgres_inspector";
}

/**
 * Static registration for the Postgres inspector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(PostgresInspectorConfigFactory,
                 Server::Configuration::NamedListenerFilterConfigFactory){
    "envoy.listener.postgres_inspector"};

} // namespace PostgresInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
