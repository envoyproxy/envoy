#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

#include <algorithm>

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

namespace {

// Sets `key` to `value` in `attributes`, overriding any attribute with the same key.
void upsertAttribute(std::vector<std::pair<std::string, std::string>>& attributes,
                     absl::string_view key, absl::string_view value) {
  for (auto& [existing_key, existing_value] : attributes) {
    if (existing_key == key) {
      existing_value = std::string(value);
      return;
    }
  }
  attributes.emplace_back(std::string(key), std::string(value));
}

} // namespace

ConversionAction::ConversionAction(
    const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config,
    const ActionContext& context)
    : config_(config) {
  if (!config_.has_resource_conversion()) {
    return;
  }
  const auto& resource_conversion = config_.resource_conversion();
  ResourceConversion& resolved = resource_conversion_.emplace();
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(resource_conversion, inherit_resource_attributes, true) &&
      context.resource_attributes != nullptr) {
    for (const auto& attribute : *context.resource_attributes) {
      upsertAttribute(resolved.base_attributes, attribute.key(), attribute.value().string_value());
    }
  }
  for (const auto& attribute : resource_conversion.static_resource_attributes()) {
    upsertAttribute(resolved.base_attributes, attribute.key(), attribute.value().string_value());
  }
  std::sort(resolved.base_attributes.begin(), resolved.base_attributes.end());

  for (const auto& tag : resource_conversion.tags_to_resource_attributes()) {
    resolved.tag_to_attribute_keys[tag.tag_name()].push_back(
        tag.resource_attribute_key().empty() ? tag.tag_name() : tag.resource_attribute_key());
  }
}

REGISTER_FACTORY(ConversionActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);
REGISTER_FACTORY(DropActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
