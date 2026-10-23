#include "source/extensions/stat_sinks/open_telemetry/stat_match_action.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {

namespace {

// Records `error` as the creation status of `context`, unless an earlier error was recorded.
void recordError(ActionContext& context, absl::string_view error) {
  if (context.creation_status.ok()) {
    context.creation_status =
        absl::InvalidArgumentError(absl::StrCat("resource_conversion: ", error));
  }
}

} // namespace

ConversionAction::ConversionAction(
    const envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig::ConversionAction& config,
    ActionContext& context)
    : config_(config) {
  if (!config_.has_resource_conversion()) {
    return;
  }
  const auto& resource_conversion = config_.resource_conversion();
  ResourceConversion& resolved = resource_conversion_.emplace();

  // Each attribute of the resource is set once, either statically or by a tag, so that no
  // attribute depends on the order in which they are set.
  absl::flat_hash_set<absl::string_view> keys;
  const auto add_key = [&keys, &context](absl::string_view key) {
    if (keys.insert(key).second) {
      return true;
    }
    recordError(context, absl::StrCat("resource attribute '", key, "' is set more than once"));
    return false;
  };

  for (const auto& attribute : resource_conversion.static_resource_attributes()) {
    if (attribute.key().empty()) {
      recordError(context, "the keys of static_resource_attributes must not be empty");
    } else if (!attribute.value().has_string_value()) {
      recordError(context, absl::StrCat("the value of static resource attribute '", attribute.key(),
                                        "' must be a string"));
    } else if (add_key(attribute.key())) {
      resolved.static_attributes.emplace_back(attribute.key(), attribute.value().string_value());
    }
  }

  for (const auto& tag : resource_conversion.tags_to_resource_attributes()) {
    const std::string& key =
        tag.resource_attribute_key().empty() ? tag.tag_name() : tag.resource_attribute_key();
    if (!add_key(key)) {
      continue;
    }
    const auto [it, inserted] = resolved.tag_indexes.try_emplace(
        tag.tag_name(), static_cast<uint32_t>(resolved.tags.size()));
    if (inserted) {
      resolved.tags.push_back({tag.tag_name(), {}});
    }
    resolved.tags[it->second].attribute_keys.push_back(key);
  }
}

REGISTER_FACTORY(ConversionActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);
REGISTER_FACTORY(DropActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
