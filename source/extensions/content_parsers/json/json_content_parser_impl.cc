#include "source/extensions/content_parsers/json/json_content_parser_impl.h"

#include "source/common/json/json_loader.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace ContentParsers {
namespace Json {

namespace {
constexpr absl::string_view DefaultNamespace = "envoy.content_parsers.json";
}

JsonContentParserImpl::Rule::Rule(const ProtoRule& rule, uint32_t stop_processing_after_matches)
    : rule_(rule), stop_processing_after_matches_(stop_processing_after_matches) {
  for (const auto& selector : rule_.selectors()) {
    selector_path_.push_back(selector.key());
  }
}

JsonContentParserImpl::JsonContentParserImpl(
    const envoy::extensions::content_parsers::json::v3::JsonContentParser& config) {
  rules_.reserve(config.rules().size());
  for (const auto& rule_config : config.rules()) {
    rules_.emplace_back(rule_config.rule(), rule_config.stop_processing_after_matches());
  }
}

ContentParser::ParseResult JsonContentParserImpl::parse(absl::string_view data) {
  ContentParser::ParseResult result;

  // Try to parse JSON
  auto json_or = Envoy::Json::Factory::loadFromString(std::string(data));
  if (!json_or.ok()) {
    ENVOY_LOG(trace, "Failed to parse JSON: {}", json_or.status().message());
    result.error_message = std::string(json_or.status().message());
    any_parse_error_ = true;
    return result;
  }
  Envoy::Json::ObjectSharedPtr json_obj = json_or.value();

  // Apply each rule and track stop processing condition.
  // We stop processing when all rules have limits and all those limits have been reached.
  // If any rule has no limit (stop_processing_after_matches == 0), we never stop.
  bool all_rules_have_limits = true;
  bool all_limited_rules_satisfied = true;

  for (size_t i = 0; i < rules_.size(); ++i) {
    auto& rule = rules_[i];

    // Track if any rule has no limit
    if (rule.stop_processing_after_matches_ == 0) {
      all_rules_have_limits = false;
    }

    // Skip rules that have already reached their match limit
    if (rule.stop_processing_after_matches_ > 0 &&
        rule.match_count_ >= rule.stop_processing_after_matches_) {
      continue;
    }

    auto value_or = extractValueFromJson(json_obj, rule.selector_path_);

    if (value_or.ok()) {
      // Selector found. Execute on_present immediately (if configured).
      const auto& value = value_or.value();

      if (rule.rule_.has_on_present()) {
        result.immediate_actions.push_back(keyValuePairToAction(rule.rule_.on_present(), value));
      }

      // Track that this rule matched
      rule.ever_matched_ = true;

      // Increment match count for this rule
      rule.match_count_++;
    } else {
      // Selector not found
      ENVOY_LOG(trace, "Selector not found: {}", value_or.status().message());
      rule.selector_not_found_ = true;
    }

    // After processing, check if this rule with a limit is still not satisfied
    if (rule.stop_processing_after_matches_ > 0 &&
        rule.match_count_ < rule.stop_processing_after_matches_) {
      all_limited_rules_satisfied = false;
    }
  }

  result.stop_processing = all_rules_have_limits && all_limited_rules_satisfied;
  return result;
}

std::vector<ContentParser::MetadataAction> JsonContentParserImpl::getAllDeferredActions() {
  std::vector<ContentParser::MetadataAction> actions;

  for (const auto& rule : rules_) {
    // Only process rules that never matched (on_present never fired)
    if (rule.ever_matched_) {
      continue;
    }

    // Priority: on_error over on_missing.
    // If any_parse_error_ is true but on_error is not configured, fall through to on_missing.
    // This allows users to handle missing values even when parse errors occur,
    // if they choose not to configure on_error handling.
    if (any_parse_error_ && rule.rule_.has_on_error()) {
      actions.push_back(keyValuePairToAction(rule.rule_.on_error(), absl::nullopt));
    } else if (rule.selector_not_found_ && rule.rule_.has_on_missing()) {
      actions.push_back(keyValuePairToAction(rule.rule_.on_missing(), absl::nullopt));
    }
  }

  return actions;
}

absl::StatusOr<Envoy::Json::ValueType>
JsonContentParserImpl::extractValueFromJson(const Envoy::Json::ObjectSharedPtr& json_obj,
                                            const std::vector<std::string>& path) const {
  if (path.empty()) {
    return absl::InvalidArgumentError("Path is empty");
  }

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

ContentParser::MetadataAction JsonContentParserImpl::keyValuePairToAction(
    const KeyValuePair& kv_pair,
    const absl::optional<Envoy::Json::ValueType>& extracted_value) const {
  ContentParser::MetadataAction action;

  // Namespace: Parser is responsible for applying the default namespace.
  // The filter expects namespace to always be populated.
  action.namespace_ = kv_pair.metadata_namespace().empty() ? std::string(DefaultNamespace)
                                                           : kv_pair.metadata_namespace();
  action.key = kv_pair.key();
  action.preserve_existing = kv_pair.preserve_existing_metadata_value();

  // Populate the metadata value:
  // 1. If kv_pair has an explicit 'value' field (kValue), use that constant value.
  //    This allows hardcoded values for on_error/on_missing fallbacks, or for
  //    on_present when only presence detection is needed (ignoring extracted value).
  // 2. Otherwise, if we extracted a value from JSON, convert it using the specified type.
  // 3. If neither, action.value remains default-constructed (empty).
  if (kv_pair.value_type_case() == KeyValuePair::kValue) {
    action.value = kv_pair.value();
  } else if (extracted_value.has_value()) {
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
    absl::visit(
        [&pb_value](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>) {
            pb_value.set_string_value(v);
          } else if constexpr (std::is_same_v<T, int64_t>) {
            pb_value.set_string_value(absl::StrCat(v));
          } else if constexpr (std::is_same_v<T, double>) {
            pb_value.set_string_value(absl::StrCat(v));
          } else if constexpr (std::is_same_v<T, bool>) {
            pb_value.set_string_value(v ? "true" : "false");
          }
        },
        value);
    break;

  case ValueType::JsonToMetadata_ValueType_NUMBER:
    // Convert to number
    absl::visit(
        [&pb_value](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, int64_t>) {
            pb_value.set_number_value(static_cast<double>(v));
          } else if constexpr (std::is_same_v<T, double>) {
            pb_value.set_number_value(v);
          } else if constexpr (std::is_same_v<T, bool>) {
            pb_value.set_number_value(v ? 1.0 : 0.0);
          } else if constexpr (std::is_same_v<T, std::string>) {
            // Try to parse string as number
            double num;
            if (absl::SimpleAtod(v, &num)) {
              pb_value.set_number_value(num);
            } else {
              // If conversion fails, leave pb_value unset (kind_case == 0)
              ENVOY_LOG_MISC(debug, "Failed to convert string '{}' to NUMBER type", v);
            }
          }
        },
        value);
    break;

  case ValueType::JsonToMetadata_ValueType_PROTOBUF_VALUE:
  default:
    // Preserve original type
    absl::visit(
        [&pb_value](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, std::string>) {
            pb_value.set_string_value(v);
          } else if constexpr (std::is_same_v<T, int64_t>) {
            pb_value.set_number_value(static_cast<double>(v));
          } else if constexpr (std::is_same_v<T, double>) {
            pb_value.set_number_value(v);
          } else if constexpr (std::is_same_v<T, bool>) {
            pb_value.set_bool_value(v);
          }
        },
        value);
    break;
  }

  return pb_value;
}

JsonContentParserFactory::JsonContentParserFactory(
    const envoy::extensions::content_parsers::json::v3::JsonContentParser& config)
    : config_(config) {}

ContentParser::ParserPtr JsonContentParserFactory::createParser() {
  return std::make_unique<JsonContentParserImpl>(config_);
}

const std::string& JsonContentParserFactory::statsPrefix() const {
  CONSTRUCT_ON_FIRST_USE(std::string, "json.");
}

} // namespace Json
} // namespace ContentParsers
} // namespace Extensions
} // namespace Envoy
