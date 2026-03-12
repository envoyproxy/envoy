#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

absl::StatusOr<std::shared_ptr<ConversionAction>> ConversionAction::create(
    const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config) {
  if (!config.metric_name().empty() && !config.metric_prefix().empty()) {
    return absl::InvalidArgumentError("Cannot specify both metric_name and metric_prefix.");
  }
  return std::shared_ptr<ConversionAction>(new ConversionAction(config));
}

absl::StatusOr<Matcher::ActionConstSharedPtr>
ConversionActionFactory::createAction(const Protobuf::Message& config, ActionContext&,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& action_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction&>(
      config, validation_visitor);
  return ConversionAction::create(action_config);
}

REGISTER_FACTORY(ConversionActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);
REGISTER_FACTORY(DropActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
