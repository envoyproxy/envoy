#include "source/extensions/filters/http/stream_to_metadata/filter.h"

#include <functional>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/http/sse/sse_parser.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {

namespace {
constexpr absl::string_view DefaultSseContentType{"text/event-stream"};

// Normalize content-type by stripping parameters and whitespace.
// Match on type/subtype only and ignore parameters.
// Returns a string_view into the original content_type.
absl::string_view normalizeContentType(absl::string_view content_type) {
  return StringUtil::trim(StringUtil::cropRight(content_type, ";"));
}

} // namespace

Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  if (rule_.selector().has_json_path()) {
    for (const auto& key : rule_.selector().json_path().path()) {
      selector_path_.push_back(key);
    }
  }
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata& config,
    Stats::Scope& scope)
    : stats_{ALL_STREAM_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "stream_to_metadata.resp"))},
      format_(config.response_rules().format()), rules_(std::invoke([&config]() {
        Rules rules;
        for (const auto& rule : config.response_rules().rules()) {
          rules.emplace_back(rule);
        }
        return rules;
      })),
      allowed_content_types_(std::invoke([&config]() {
        absl::flat_hash_set<std::string> types;
        if (config.response_rules().allowed_content_types().empty()) {
          types.insert(std::string(DefaultSseContentType));
        } else {
          for (const auto& type : config.response_rules().allowed_content_types()) {
            types.insert(std::string(normalizeContentType(type)));
          }
        }
        return types;
      })),
      max_event_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.response_rules(), max_event_size, 8192)) {}

bool FilterConfig::isContentTypeAllowed(absl::string_view content_type) const {
  return allowed_content_types_.contains(normalizeContentType(content_type));
}

Filter::Filter(std::shared_ptr<FilterConfig> config)
    : config_(std::move(config)), rule_states_(config_->rules().size()) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  absl::string_view content_type = headers.getContentTypeValue();
  if (!content_type.empty()) {
    if (config_->isContentTypeAllowed(content_type)) {
      content_type_matched_ = true;
    } else {
      ENVOY_LOG(trace, "Content-Type not allowed: {}", content_type);
      config_->stats().mismatched_content_type_.inc();
    }
  } else {
    ENVOY_LOG(trace, "Missing Content-Type header (SSE streams require text/event-stream)");
    config_->stats().mismatched_content_type_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!content_type_matched_ || processing_complete_) {
    // If we're at end_stream but never processed, execute on_error for all rules
    if (end_stream && !content_type_matched_) {
      // Mark all rules as having an error (content-type mismatch is an error condition)
      for (auto& state : rule_states_) {
        state.has_error_occurred = true;
      }
      finalizeRules();
    }
    return Http::FilterDataStatus::Continue;
  }

  const uint64_t data_length = data.length();
  if (data_length == 0 && !end_stream) {
    return Http::FilterDataStatus::Continue;
  }

  if (data_length > 0) {
    buffer_.append(data.toString());
    processBuffer(end_stream);
  }

  // Finalize rules at end of stream or when processing stopped early
  if (end_stream || processing_complete_) {
    finalizeRules();
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  // Finalize rules if not already done.
  if (!processing_complete_) {
    if (!content_type_matched_) {
      // Mark all rules as having an error (content-type mismatch)
      for (auto& state : rule_states_) {
        state.has_error_occurred = true;
      }
    }
    finalizeRules();
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::processBuffer(bool end_stream) {
  absl::string_view buffer_view(buffer_);

  while (!buffer_view.empty() && !processing_complete_) {
    auto [event_end, next_event] = Http::Sse::SseParser::findEventEnd(buffer_view, end_stream);

    if (event_end == absl::string_view::npos) {
      // No complete event found. Check if buffer exceeds max size.
      const uint32_t max_size = config_->maxEventSize();
      if (max_size > 0 && buffer_view.size() > max_size) {
        ENVOY_LOG(
            warn,
            "SSE event exceeds max_event_size ({} bytes). Discarding {} bytes of buffered data.",
            max_size, buffer_view.size());
        config_->stats().event_too_large_.inc();
        buffer_.clear();
        return;
      }
      break;
    }

    absl::string_view event = buffer_view.substr(0, event_end);
    ENVOY_LOG(trace, "Processing SSE event: {}", absl::CEscape(event));

    if (processSseEvent(event)) {
      processing_complete_ = true;
      break;
    }

    buffer_view = buffer_view.substr(next_event);
  }

  // Keep only the unprocessed tail substring (remove processed bytes from front).
  buffer_.erase(0, buffer_.size() - buffer_view.size());
}

bool Filter::processSseEvent(absl::string_view event) {
  const std::string json_string = Http::Sse::SseParser::extractDataField(event);

  if (json_string.empty()) {
    ENVOY_LOG(debug, "Event does not contain 'data' field");
    config_->stats().no_data_field_.inc();
    // Track error for all rules (will execute on_error at end if on_present never executes)
    for (auto& state : rule_states_) {
      state.has_error_occurred = true;
    }
    return false;
  }

  absl::StatusOr<Json::ObjectSharedPtr> result = Json::Factory::loadFromString(json_string);
  if (!result.ok()) {
    ENVOY_LOG(debug, "Failed to parse JSON from data field: {}", result.status().message());
    config_->stats().invalid_json_.inc();
    // Track error for all rules (will execute on_error at end if on_present never executes)
    for (auto& state : rule_states_) {
      state.has_error_occurred = true;
    }
    return false;
  }

  const auto& json_obj = result.value();

  for (size_t i = 0; i < config_->rules().size(); ++i) {
    if (applyRule(json_obj, i)) {
      return true;
    }
  }

  return false;
}

bool Filter::applyRule(const Json::ObjectSharedPtr& json_obj, size_t rule_index) {
  const auto& rule = config_->rules()[rule_index];
  auto& state = rule_states_[rule_index];
  auto value_or = extractValueFromJson(json_obj, rule.selector_path_);

  if (value_or.ok()) {
    // Selector found. Execute on_present immediately and mark as succeeded.
    const auto& value = value_or.value();
    writeOnPresent(value, rule.rule_.on_present());
    config_->stats().success_.inc();
    state.has_on_present_executed = true;

    return rule.rule_.stop_processing_on_match();
  } else {
    // Selector not found. Track for on_missing (will execute at end if on_present never executes).
    ENVOY_LOG(trace, "Selector not found: {}", value_or.status().message());
    config_->stats().selector_not_found_.inc();
    state.has_missing_occurred = true;
    return false;
  }
}

void Filter::writeOnPresent(const Json::ValueType& extracted_value,
                            const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors) {
  for (const auto& descriptor : descriptors) {
    writeMetadata(extracted_value, descriptor);
  }
}

void Filter::writeOnMissing(const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors) {
  for (const auto& descriptor : descriptors) {
    writeMetadata(absl::nullopt, descriptor);
  }
}

void Filter::writeOnError(const Protobuf::RepeatedPtrField<MetadataDescriptor>& descriptors) {
  for (const auto& descriptor : descriptors) {
    writeMetadata(absl::nullopt, descriptor);
  }
}

void Filter::finalizeRules() {
  for (size_t i = 0; i < config_->rules().size(); ++i) {
    const auto& rule = config_->rules()[i];
    const auto& state = rule_states_[i];

    // If on_present never executed, execute on_error (priority) or on_missing
    if (!state.has_on_present_executed) {
      if (state.has_error_occurred) {
        ENVOY_LOG(trace, "Rule {}: executing on_error (errors occurred, on_present never executed)",
                  i);
        writeOnError(rule.rule_.on_error());
      } else if (state.has_missing_occurred) {
        ENVOY_LOG(trace,
                  "Rule {}: executing on_missing (selector not found, on_present never executed)",
                  i);
        writeOnMissing(rule.rule_.on_missing());
      }
    }
  }
}

absl::StatusOr<Json::ValueType>
Filter::extractValueFromJson(const Json::ObjectSharedPtr& json_obj,
                             const std::vector<std::string>& path) const {
  if (path.empty()) {
    return absl::InvalidArgumentError("Empty selector path");
  }

  Json::ObjectSharedPtr current = json_obj;

  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto child_or = current->getObject(path[i]);
    if (!child_or.ok()) {
      return absl::NotFoundError(
          absl::StrCat("Key '", path[i], "' not found or not an object at path index ", i));
    }
    current = child_or.value();
  }

  const std::string& final_key = path.back();

  auto string_val = current->getString(final_key);
  if (string_val.ok()) {
    return Json::ValueType{string_val.value()};
  }

  auto int_val = current->getInteger(final_key);
  if (int_val.ok()) {
    return Json::ValueType{int_val.value()};
  }

  auto double_val = current->getDouble(final_key);
  if (double_val.ok()) {
    return Json::ValueType{double_val.value()};
  }

  auto bool_val = current->getBoolean(final_key);
  if (bool_val.ok()) {
    return Json::ValueType{bool_val.value()};
  }

  auto obj_val = current->getObject(final_key);
  if (obj_val.ok()) {
    // Convert object to JSON string
    return Json::ValueType{obj_val.value()->asJsonString()};
  }

  return absl::NotFoundError(absl::StrCat("Key '", final_key, "' not found"));
}

void Filter::writeMetadata(const absl::optional<Json::ValueType>& extracted_value,
                           const MetadataDescriptor& descriptor) {
  const std::string namespace_str = descriptor.metadata_namespace().empty()
                                        ? "envoy.filters.http.stream_to_metadata"
                                        : descriptor.metadata_namespace();

  // Check preserve_existing_metadata_value
  if (PROTOBUF_GET_WRAPPED_OR_DEFAULT(descriptor, preserve_existing_metadata_value, false)) {
    const auto& filter_metadata =
        encoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto entry_it = filter_metadata.find(namespace_str);
    if (entry_it != filter_metadata.end()) {
      const auto& metadata = entry_it->second;
      if (metadata.fields().contains(descriptor.key())) {
        ENVOY_LOG(trace, "Preserving existing metadata value for key {} in namespace {}",
                  descriptor.key(), namespace_str);
        config_->stats().preserved_existing_metadata_.inc();
        return;
      }
    }
  }

  // Determine which value to use: hardcoded or extracted
  Protobuf::Value proto_value;

  // Check if descriptor has a hardcoded value (kind_case() != 0 means a value is set)
  if (descriptor.value().kind_case() != 0) {
    // Use hardcoded value from descriptor
    proto_value = descriptor.value();
    ENVOY_LOG(trace, "Using hardcoded value from descriptor");
  } else if (extracted_value.has_value()) {
    // Convert extracted value
    auto proto_value_or = convertToProtobufValue(extracted_value.value(), descriptor.type());
    if (!proto_value_or.ok()) {
      ENVOY_LOG(warn, "Failed to convert extracted value to protobuf: {}",
                proto_value_or.status().message());
      return;
    }
    proto_value = proto_value_or.value();
  } else {
    ENVOY_LOG(warn, "No value to write for key {} in namespace {}", descriptor.key(),
              namespace_str);
    return;
  }

  // Write to dynamic metadata
  Protobuf::Struct metadata;
  (*metadata.mutable_fields())[descriptor.key()] = proto_value;
  encoder_callbacks_->streamInfo().setDynamicMetadata(namespace_str, metadata);

  ENVOY_LOG(trace, "Wrote metadata: namespace={}, key={}", namespace_str, descriptor.key());
}

absl::StatusOr<Protobuf::Value> Filter::convertToProtobufValue(const Json::ValueType& json_value,
                                                               ValueType type) const {
  Protobuf::Value proto_value;

  if (absl::holds_alternative<std::string>(json_value)) {
    const auto& str_val = absl::get<std::string>(json_value);
    if (type == ValueType::StreamToMetadata_ValueType_NUMBER) {
      double num;
      if (absl::SimpleAtod(str_val, &num)) {
        proto_value.set_number_value(num);
      } else {
        return absl::InvalidArgumentError("Cannot convert string to number");
      }
    } else {
      proto_value.set_string_value(str_val);
    }
  } else if (absl::holds_alternative<int64_t>(json_value)) {
    const auto num_val = absl::get<int64_t>(json_value);
    if (type == ValueType::StreamToMetadata_ValueType_STRING) {
      proto_value.set_string_value(absl::StrCat(num_val));
    } else {
      proto_value.set_number_value(static_cast<double>(num_val));
    }
  } else if (absl::holds_alternative<double>(json_value)) {
    const auto num_val = absl::get<double>(json_value);
    if (type == ValueType::StreamToMetadata_ValueType_STRING) {
      proto_value.set_string_value(absl::StrCat(num_val));
    } else {
      proto_value.set_number_value(num_val);
    }
  } else if (absl::holds_alternative<bool>(json_value)) {
    const auto bool_val = absl::get<bool>(json_value);
    if (type == ValueType::StreamToMetadata_ValueType_STRING) {
      proto_value.set_string_value(bool_val ? "true" : "false");
    } else if (type == ValueType::StreamToMetadata_ValueType_NUMBER) {
      proto_value.set_number_value(bool_val ? 1.0 : 0.0);
    } else {
      proto_value.set_bool_value(bool_val);
    }
  } else {
    return absl::InvalidArgumentError("Unsupported JSON value type");
  }

  return proto_value;
}

} // namespace StreamToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
