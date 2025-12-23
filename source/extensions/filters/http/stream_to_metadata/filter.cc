#include "source/extensions/filters/http/stream_to_metadata/filter.h"

#include <string>

#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

#include "absl/strings/match.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StreamToMetadata {

namespace {
constexpr absl::string_view DefaultSseContentType{"text/event-stream"};
} // namespace

Rule::Rule(const ProtoRule& rule) : rule_(rule) {
  // Validate that exactly one selector type is set
  if (!rule_.selector().has_json_path()) {
    throw EnvoyException("Selector must have json_path specified");
  }

  for (const auto& key : rule_.selector().json_path().path()) {
    selector_path_.push_back(key);
  }
}

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::stream_to_metadata::v3::StreamToMetadata& config,
    Stats::Scope& scope)
    : stats_{ALL_STREAM_TO_METADATA_FILTER_STATS(
          POOL_COUNTER_PREFIX(scope, "stream_to_metadata."))},
      format_(config.format()), rules_([&config]() {
        Rules rules;
        for (const auto& rule : config.rules()) {
          rules.emplace_back(rule);
        }
        return rules;
      }()),
      allowed_content_types_([&config]() {
        absl::flat_hash_set<std::string> types;
        if (config.allowed_content_types().empty()) {
          types.insert(std::string(DefaultSseContentType));
        } else {
          for (const auto& type : config.allowed_content_types()) {
            types.insert(type);
          }
        }
        return types;
      }()),
      max_event_size_(config.has_max_event_size() ? config.max_event_size().value() : 8192) {}

bool FilterConfig::isContentTypeAllowed(absl::string_view content_type) const {
  return allowed_content_types_.contains(content_type);
}

Filter::Filter(std::shared_ptr<FilterConfig> config) : config_(std::move(config)) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  const absl::string_view content_type = headers.getContentTypeValue();
  if (!content_type.empty()) {
    if (config_->isContentTypeAllowed(content_type)) {
      should_process_ = true;
    } else {
      ENVOY_LOG(trace, "Content-Type not allowed: {}", content_type);
      config_->stats().mismatched_content_type_.inc();
    }
  } else {
    ENVOY_LOG(trace, "No Content-Type header found");
    config_->stats().mismatched_content_type_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!should_process_ || stop_processing_) {
    return Http::FilterDataStatus::Continue;
  }

  const uint64_t data_length = data.length();
  if (data_length == 0) {
    return Http::FilterDataStatus::Continue;
  }

  buffer_.append(static_cast<const char*>(data.linearize(data_length)), data_length);
  processBuffer(end_stream);

  return Http::FilterDataStatus::Continue;
}

void Filter::processBuffer(bool end_stream) {
  absl::string_view buffer_view(buffer_);

  while (!buffer_view.empty() && !stop_processing_) {
    auto [event_end, next_event] = findEventEnd(buffer_view, end_stream);

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
      stop_processing_ = true;
      break;
    }

    buffer_view = buffer_view.substr(next_event);
  }

  buffer_.erase(0, buffer_.size() - buffer_view.size());
}

bool Filter::processSseEvent(absl::string_view event) {
  const std::string json_string = extractSseDataField(event);

  if (json_string.empty()) {
    ENVOY_LOG(debug, "Event does not contain 'data' field");
    config_->stats().no_data_field_.inc();
    return false;
  }

  absl::StatusOr<Json::ObjectSharedPtr> result = Json::Factory::loadFromString(json_string);
  if (!result.ok()) {
    ENVOY_LOG(debug, "Failed to parse JSON from data field: {}", result.status().message());
    config_->stats().invalid_json_.inc();
    return false;
  }

  const auto& json_obj = result.value();
  bool should_stop = false;

  for (const auto& rule : config_->rules()) {
    if (applyRule(json_obj, rule)) {
      config_->stats().success_.inc();

      if (rule.rule_.stop_processing_on_match()) {
        should_stop = true;
        break;
      }
    }
  }

  return should_stop;
}

std::string Filter::extractSseDataField(absl::string_view event) const {
  std::string result;
  absl::string_view remaining = event;

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, true);
    absl::string_view line = remaining.substr(0, line_end);
    remaining = remaining.substr(next_line);

    auto [field_name, field_value] = parseSseFieldLine(line);
    if (field_name == "data") {
      if (!result.empty()) {
        // Per SSE spec, multiple data fields are concatenated with newline.
        result += '\n';
      }
      result.append(field_value.data(), field_value.size());
    }
  }

  return result;
}

std::pair<absl::string_view, absl::string_view>
Filter::parseSseFieldLine(absl::string_view line) const {
  if (line.empty()) {
    return {"", ""};
  }

  // Per SSE spec, lines starting with ':' are comments and should be ignored.
  if (line[0] == ':') {
    return {"", ""};
  }

  const auto colon_pos = line.find(':');
  if (colon_pos == absl::string_view::npos) {
    return {line, ""};
  }

  absl::string_view field_name = line.substr(0, colon_pos);
  absl::string_view field_value = line.substr(colon_pos + 1);

  // Per SSE spec, remove leading space from value if present.
  if (!field_value.empty() && field_value[0] == ' ') {
    field_value = field_value.substr(1);
  }

  return {field_name, field_value};
}

std::pair<size_t, size_t> Filter::findLineEnd(absl::string_view str, bool end_stream) const {
  const auto pos = str.find_first_of("\r\n");

  if (pos == absl::string_view::npos) {
    if (end_stream) {
      return {str.size(), str.size()};
    }
    return {absl::string_view::npos, absl::string_view::npos};
  }

  if (str[pos] == '\n') {
    return {pos, pos + 1};
  }

  // Per SSE spec, handle CR (\r) and CRLF (\r\n) line endings.
  if (pos + 1 < str.size()) {
    if (str[pos + 1] == '\n') {
      return {pos, pos + 2};
    }
    return {pos, pos + 1};
  }

  // If '\r' is at the end and more data may come, wait to see if it's CRLF.
  if (end_stream) {
    return {pos, pos + 1};
  }
  return {absl::string_view::npos, absl::string_view::npos};
}

std::pair<size_t, size_t> Filter::findEventEnd(absl::string_view str, bool end_stream) const {
  size_t consumed = 0;
  absl::string_view remaining = str;

  while (!remaining.empty()) {
    auto [line_end, next_line] = findLineEnd(remaining, end_stream);

    if (line_end == absl::string_view::npos) {
      return {absl::string_view::npos, absl::string_view::npos};
    }

    if (line_end == 0) {
      return {consumed, consumed + next_line};
    }

    consumed += next_line;
    remaining = remaining.substr(next_line);
  }

  return {absl::string_view::npos, absl::string_view::npos};
}

bool Filter::applyRule(const Json::ObjectSharedPtr& json_obj, const Rule& rule) {
  auto value_or = extractValueFromJson(json_obj, rule.selector_path_);
  if (!value_or.ok()) {
    ENVOY_LOG(trace, "Selector not found: {}", value_or.status().message());
    config_->stats().selector_not_found_.inc();
    return false;
  }

  const auto& value = value_or.value();
  bool wrote_any = false;

  for (const auto& descriptor : rule.rule_.metadata_descriptors()) {
    if (writeMetadata(value, descriptor)) {
      wrote_any = true;
    }
  }

  return wrote_any;
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
    return Json::ValueType{static_cast<double>(int_val.value())};
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

bool Filter::writeMetadata(const Json::ValueType& value, const MetadataDescriptor& descriptor) {
  if (descriptor.preserve_existing_metadata_value()) {
    const auto& filter_metadata =
        encoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto entry_it = filter_metadata.find(descriptor.metadata_namespace());
    if (entry_it != filter_metadata.end()) {
      const auto& metadata = entry_it->second;
      if (metadata.fields().contains(descriptor.key())) {
        ENVOY_LOG(trace, "Preserving existing metadata value for key {} in namespace {}",
                  descriptor.key(), descriptor.metadata_namespace());
        config_->stats().preserved_existing_metadata_.inc();
        return false;
      }
    }
  }

  auto proto_value_or = convertToProtobufValue(value, descriptor.type());
  if (!proto_value_or.ok()) {
    ENVOY_LOG(warn, "Failed to convert value to protobuf: {}", proto_value_or.status().message());
    return false;
  }

  Protobuf::Struct metadata;
  (*metadata.mutable_fields())[descriptor.key()] = proto_value_or.value();
  encoder_callbacks_->streamInfo().setDynamicMetadata(descriptor.metadata_namespace(), metadata);

  ENVOY_LOG(trace, "Wrote metadata: namespace={}, key={}", descriptor.metadata_namespace(),
            descriptor.key());
  return true;
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
