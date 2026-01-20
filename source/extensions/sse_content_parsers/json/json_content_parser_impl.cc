#include "source/extensions/sse_content_parsers/json/json_content_parser_impl.h"

#include "source/common/json/json_loader.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace SseContentParsers {
namespace Json {

namespace {
constexpr absl::string_view DefaultNamespace = "envoy.filters.http.sse_to_metadata";
}

JsonContentParserImpl::Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  for (const auto& selector : rule_.selectors()) {
    selector_path_.push_back(selector.key());
  }
}

JsonContentParserImpl::JsonContentParserImpl(
    const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser& config)
    : stop_processing_on_first_match_(config.stop_processing_on_first_match()) {
  for (const auto& rule : config.rules()) {
    rules_.emplace_back(rule);
  }
}

SseContentParser::ParseResult JsonContentParserImpl::parse(absl::string_view data) {
  SseContentParser::ParseResult result;

  // Try to parse JSON
  auto json_or = Envoy::Json::Factory::loadFromString(std::string(data));
  if (!json_or.ok()) {
    ENVOY_LOG(trace, "Failed to parse JSON: {}", json_or.status().message());
    result.has_error = true;
    return result;
  }
  Envoy::Json::ObjectSharedPtr json_obj = json_or.value();

  // Apply each rule
  for (size_t i = 0; i < rules_.size(); ++i) {
    const auto& rule = rules_[i];
    auto value_or = extractValueFromJson(json_obj, rule.selector_path_);

    if (value_or.ok()) {
      // Selector found. Execute on_present immediately (if configured).
      const auto& value = value_or.value();

      if (rule.rule_.has_on_present()) {
        result.immediate_actions.push_back(keyValuePairToAction(rule.rule_.on_present(), value));
      }

      // Track that this rule matched
      result.matched_rules.push_back(i);

      // Stop processing if configured to stop on first match
      if (stop_processing_on_first_match_) {
        result.stop_processing = true;
        return result;
      }
    } else {
      // Selector not found. Mark for on_missing (will execute at end if on_present never
      // executes).
      ENVOY_LOG(trace, "Selector not found: {}", value_or.status().message());
      result.selector_not_found_rules.push_back(i);
    }
  }

  return result;
}

std::vector<SseContentParser::MetadataAction>
JsonContentParserImpl::getDeferredActions(size_t rule_index, bool has_error,
                                          bool selector_not_found) {
  std::vector<SseContentParser::MetadataAction> actions;

  // Note: rule_index is always valid - it comes from iterating rule_states_ which matches rules_
  ASSERT(rule_index < rules_.size());
  const auto& rule = rules_[rule_index];

  // Priority: on_error over on_missing
  if (has_error && rule.rule_.has_on_error()) {
    actions.push_back(keyValuePairToAction(rule.rule_.on_error(), absl::nullopt));
  } else if (selector_not_found && rule.rule_.has_on_missing()) {
    actions.push_back(keyValuePairToAction(rule.rule_.on_missing(), absl::nullopt));
  }

  return actions;
}

absl::StatusOr<Envoy::Json::ValueType>
JsonContentParserImpl::extractValueFromJson(const Envoy::Json::ObjectSharedPtr& json_obj,
                                            const std::vector<std::string>& path) const {
  // Note: path cannot be empty - validated in Rule constructor from proto (min_items: 1)
  ASSERT(!path.empty());

  Envoy::Json::ObjectSharedPtr current = json_obj;

  // Traverse path except last element
  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto child_or = current->getObject(path[i]);
    if (!child_or.ok()) {
      return absl::NotFoundError(
          absl::StrCat("Key '", path[i], "' not found or not an object at path index ", i));
    }
    current = child_or.value();
  }

  const std::string& final_key = path.back();

  // Try to extract value as different types
  auto string_val = current->getString(final_key);
  if (string_val.ok()) {
    return Envoy::Json::ValueType{string_val.value()};
  }

  auto int_val = current->getInteger(final_key);
  if (int_val.ok()) {
    return Envoy::Json::ValueType{int_val.value()};
  }

  auto double_val = current->getDouble(final_key);
  if (double_val.ok()) {
    return Envoy::Json::ValueType{double_val.value()};
  }

  auto bool_val = current->getBoolean(final_key);
  if (bool_val.ok()) {
    return Envoy::Json::ValueType{bool_val.value()};
  }

  // Try to extract as nested object and stringify
  auto obj_val = current->getObject(final_key);
  if (obj_val.ok()) {
    return Envoy::Json::ValueType{obj_val.value()->asJsonString()};
  }

  return absl::NotFoundError(absl::StrCat("Key '", final_key, "' not found"));
}

SseContentParser::MetadataAction JsonContentParserImpl::keyValuePairToAction(
    const KeyValuePair& kv_pair,
    const absl::optional<Envoy::Json::ValueType>& extracted_value) const {
  SseContentParser::MetadataAction action;

  // Namespace: Parser is responsible for applying the default namespace.
  // The filter expects namespace to always be populated.
  action.namespace_ = kv_pair.metadata_namespace().empty() ? std::string(DefaultNamespace)
                                                           : kv_pair.metadata_namespace();
  action.key = kv_pair.key();
  action.preserve_existing = kv_pair.preserve_existing_metadata_value();

  if (kv_pair.value_type_case() == KeyValuePair::kValue) {
    // Use hardcoded value from kv_pair (already a Protobuf::Value)
    action.value = kv_pair.value();
  } else if (extracted_value.has_value()) {
    // Convert extracted JSON value to Protobuf::Value
    action.value = jsonValueToProtobufValue(extracted_value.value(), kv_pair.type());
  }

  return action;
}

Protobuf::Value JsonContentParserImpl::jsonValueToProtobufValue(const Envoy::Json::ValueType& value,
                                                                ValueType type) const {
  Protobuf::Value pb_value;

  switch (type) {
  case ValueType::JsonToMetadata_ValueType_STRING:
    // Always convert to string
    if (absl::holds_alternative<std::string>(value)) {
      pb_value.set_string_value(absl::get<std::string>(value));
    } else if (absl::holds_alternative<int64_t>(value)) {
      pb_value.set_string_value(absl::StrCat(absl::get<int64_t>(value)));
    } else if (absl::holds_alternative<double>(value)) {
      pb_value.set_string_value(absl::StrCat(absl::get<double>(value)));
    } else if (absl::holds_alternative<bool>(value)) {
      pb_value.set_string_value(absl::get<bool>(value) ? "true" : "false");
    }
    break;

  case ValueType::JsonToMetadata_ValueType_NUMBER:
    // Convert to number
    if (absl::holds_alternative<int64_t>(value)) {
      pb_value.set_number_value(static_cast<double>(absl::get<int64_t>(value)));
    } else if (absl::holds_alternative<double>(value)) {
      pb_value.set_number_value(absl::get<double>(value));
    } else if (absl::holds_alternative<bool>(value)) {
      pb_value.set_number_value(absl::get<bool>(value) ? 1.0 : 0.0);
    } else if (absl::holds_alternative<std::string>(value)) {
      // Try to parse string as number
      double num;
      if (absl::SimpleAtod(absl::get<std::string>(value), &num)) {
        pb_value.set_number_value(num);
      } else {
        // If conversion fails, leave pb_value unset (kind_case == 0)
        ENVOY_LOG(debug, "Failed to convert string '{}' to NUMBER type",
                  absl::get<std::string>(value));
      }
    }
    break;

  case ValueType::JsonToMetadata_ValueType_PROTOBUF_VALUE:
  default:
    // Preserve original type
    if (absl::holds_alternative<std::string>(value)) {
      pb_value.set_string_value(absl::get<std::string>(value));
    } else if (absl::holds_alternative<int64_t>(value)) {
      pb_value.set_number_value(static_cast<double>(absl::get<int64_t>(value)));
    } else if (absl::holds_alternative<double>(value)) {
      pb_value.set_number_value(absl::get<double>(value));
    } else if (absl::holds_alternative<bool>(value)) {
      pb_value.set_bool_value(absl::get<bool>(value));
    }
    break;
  }

  return pb_value;
}

JsonContentParserFactory::JsonContentParserFactory(
    const envoy::extensions::sse_content_parsers::json::v3::JsonContentParser& config)
    : config_(config) {}

SseContentParser::ParserPtr JsonContentParserFactory::createParser() {
  return std::make_unique<JsonContentParserImpl>(config_);
}

const std::string& JsonContentParserFactory::statsPrefix() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "json.");
}

} // namespace Json
} // namespace SseContentParsers
} // namespace Extensions
} // namespace Envoy
