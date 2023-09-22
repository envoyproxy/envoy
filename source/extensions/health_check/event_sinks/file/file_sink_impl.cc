#include "source/extensions/health_check/event_sinks/file/file_sink_impl.h"

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

void HealthCheckEventFileSink::log(envoy::data::core::v3::HealthCheckEvent event) {
#ifdef ENVOY_ENABLE_YAML
  // Make sure the type enums make it into the JSON
  const auto json =
      MessageUtil::getJsonStringFromMessageOrError(event, /* pretty_print */ false,
                                                   /* always_print_primitive_fields */ true);
  file_->write(fmt::format("{}\n", json));
#endif
};

HealthCheckEventSinkPtr HealthCheckEventFileSinkFactory::createHealthCheckEventSink(
    const ProtobufWkt::Any& config, Server::Configuration::HealthCheckerFactoryContext& context) {
  const auto& validator_config = Envoy::MessageUtil::anyConvertAndValidate<
      envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink>(
      config, context.messageValidationVisitor());
  return std::make_unique<HealthCheckEventFileSink>(validator_config, context.accessLogManager());
}

REGISTER_FACTORY(HealthCheckEventFileSinkFactory, HealthCheckEventSinkFactory);

} // namespace Upstream
} // namespace Envoy
