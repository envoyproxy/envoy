#include "source/extensions/filters/http/aws_eventstream_parser/filter.h"

#include <functional>
#include <string>

#include "source/common/common/hex.h"
#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/extensions/common/aws/eventstream/eventstream_parser.h"

#include "absl/strings/match.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsEventstreamParser {

namespace {
constexpr absl::string_view EventstreamContentType{"application/vnd.amazon.eventstream"};

// Check if content type is application/vnd.amazon.eventstream, ignoring parameters.
// HTTP Content-Type is case-insensitive.
bool isEventstreamContentType(absl::string_view content_type) {
  absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, EventstreamContentType);
}

} // namespace

FilterConfig::FilterConfig(const ProtoConfig& config,
                           Server::Configuration::ServerFactoryContext& context)
    : parser_factory_(std::invoke([&config, &context]() {
        // Create the parser factory from TypedExtensionConfig
        auto& factory =
            Config::Utility::getAndCheckFactory<ContentParser::NamedContentParserConfigFactory>(
                config.response_rules().content_parser());
        auto message = Config::Utility::translateAnyToFactoryConfig(
            config.response_rules().content_parser().typed_config(),
            context.messageValidationVisitor(), factory);
        return factory.createParserFactory(*message, context);
      })),
      stats_(generateStats(
          fmt::format("aws_eventstream_parser.resp.{}", parser_factory_->statsPrefix()),
          context.scope())),
      header_rules_(config.response_rules().header_rules()) {}

Filter::Filter(std::shared_ptr<FilterConfig> config)
    : config_(std::move(config)), parser_(config_->parserFactory().createParser()),
      header_rule_states_(config_->headerRules().size()) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  absl::string_view content_type = headers.getContentTypeValue();
  if (!content_type.empty() && isEventstreamContentType(content_type)) {
    content_type_matched_ = true;
  } else {
    if (content_type.empty()) {
      ENVOY_LOG(trace, "Missing Content-Type header (EventStream requires "
                       "application/vnd.amazon.eventstream)");
    } else {
      ENVOY_LOG(trace, "Content-Type '{}' is not application/vnd.amazon.eventstream", content_type);
    }
    config_->stats().mismatched_content_type_.inc();
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus Filter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!content_type_matched_ || processing_complete_) {
    return Http::FilterDataStatus::Continue;
  }

  if (data.length() > 0) {
    buffer_.add(data);
    processBuffer();
  }

  // Finalize rules at end of stream or when processing stopped early
  if (end_stream || processing_complete_) {
    finalizeRules();
  }

  writeMetadata();
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  // Finalize rules if not already done (only if Content-Type matched)
  if (content_type_matched_ && !processing_complete_) {
    finalizeRules();
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::processBuffer() {
  // Linearize buffer to get contiguous memory for string_view.
  const uint64_t length = buffer_.length();
  absl::string_view buffer_view(static_cast<const char*>(buffer_.linearize(length)), length);

  while (!buffer_view.empty() && !processing_complete_) {
    auto result =
        Extensions::Common::Aws::Eventstream::EventstreamParser::parseMessage(buffer_view);

    if (!result.ok()) {
      // Parse error - corrupt data. Stream framing is broken, so we can't recover.
      ENVOY_LOG(warn, "EventStream parse error: {}", result.status().message());
      config_->stats().eventstream_error_.inc();
      buffer_.drain(buffer_.length());
      processing_complete_ = true;
      return;
    }

    if (!result->message.has_value()) {
      // Incomplete message - need more data
      break;
    }

    // Process the complete message
    const auto& message = result->message.value();
    ENVOY_LOG(trace, "Processing EventStream message with {} headers and {} bytes payload",
              message.headers.size(), message.payload_bytes.size());

    // Process EventStream headers against configured header rules.
    processMessageHeaders(message.headers);

    bool content_parser_stop = processMessage(message.payload_bytes);
    if (content_parser_stop && allHeaderRulesSatisfied()) {
      processing_complete_ = true;
      break;
    }

    buffer_view = buffer_view.substr(result->bytes_consumed);
  }

  // Drain processed bytes from the front of the buffer.
  buffer_.drain(length - buffer_view.size());
}

bool Filter::processMessage(absl::string_view payload) {
  if (payload.empty()) {
    ENVOY_LOG(debug, "Message has empty payload");
    config_->stats().empty_payload_.inc();
    return false;
  }

  auto result = parser_->parse(payload);

  if (result.error_message.has_value()) {
    ENVOY_LOG(debug, "Parser reported error: {}", result.error_message.value());
    config_->stats().parse_error_.inc();
    return false;
  }

  // Execute immediate actions returned by parser
  for (const auto& action : result.immediate_actions) {
    if (addMetadata(action)) {
      config_->stats().metadata_added_.inc();
    }
  }

  return result.stop_processing;
}

void Filter::processMessageHeaders(
    const std::vector<Extensions::Common::Aws::Eventstream::Header>& headers) {
  const auto& rules = config_->headerRules();
  for (int i = 0; i < rules.size(); ++i) {
    const auto& rule = rules[i];
    auto& state = header_rule_states_[i];

    // Skip rules that have already reached their match limit.
    if (rule.stop_processing_after_matches() > 0 &&
        state.match_count >= rule.stop_processing_after_matches()) {
      continue;
    }

    for (const auto& header : headers) {
      if (header.name == rule.header_name()) {
        if (rule.has_on_present()) {
          const auto& on_present = rule.on_present();
          ContentParser::MetadataAction action;
          action.namespace_ = on_present.metadata_namespace().empty()
                                  ? std::string(FilterConfig::DefaultHeaderNamespace)
                                  : on_present.metadata_namespace();
          action.key = on_present.key();

          if (on_present.has_value()) {
            action.value = on_present.value();
          } else {
            action.value = headerValueToProtobufValue(header.value);
          }

          if (addMetadata(action)) {
            config_->stats().metadata_added_.inc();
          }
        }
        state.ever_matched = true;
        state.match_count++;
        break;
      }
    }
  }
}

Protobuf::Value Filter::headerValueToProtobufValue(
    const Extensions::Common::Aws::Eventstream::HeaderValue& header_value) {
  Protobuf::Value pb_value;

  switch (header_value.type) {
  case Extensions::Common::Aws::Eventstream::HeaderValueType::BoolTrue:
  case Extensions::Common::Aws::Eventstream::HeaderValueType::BoolFalse:
    pb_value.set_bool_value(absl::get<bool>(header_value.value));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Byte:
    pb_value.set_number_value(static_cast<double>(absl::get<int8_t>(header_value.value)));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Short:
    pb_value.set_number_value(static_cast<double>(absl::get<int16_t>(header_value.value)));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Int32:
    pb_value.set_number_value(static_cast<double>(absl::get<int32_t>(header_value.value)));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Int64:
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Timestamp:
    pb_value.set_number_value(static_cast<double>(absl::get<int64_t>(header_value.value)));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::String:
    pb_value.set_string_value(absl::get<std::string>(header_value.value));
    break;
  case Extensions::Common::Aws::Eventstream::HeaderValueType::ByteArray: {
    const auto& bytes = absl::get<std::string>(header_value.value);
    pb_value.set_string_value(
        Hex::encode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    break;
  }
  case Extensions::Common::Aws::Eventstream::HeaderValueType::Uuid: {
    const auto& uuid = absl::get<std::array<uint8_t, 16>>(header_value.value);
    pb_value.set_string_value(
        fmt::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-"
                    "{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                    uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7], uuid[8],
                    uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]));
    break;
  }
  }

  return pb_value;
}

void Filter::finalizeRules() {
  // Get all deferred actions from parser (handles on_error/on_missing for unmatched rules)
  auto deferred_actions = parser_->getAllDeferredActions();

  if (!deferred_actions.empty()) {
    ENVOY_LOG(trace, "Executing {} deferred actions at end of stream", deferred_actions.size());
  }

  for (const auto& action : deferred_actions) {
    if (addMetadata(action)) {
      config_->stats().metadata_added_.inc();
      config_->stats().metadata_from_fallback_.inc();
    }
  }

  // Finalize header rules: apply on_missing for header rules that never matched.
  const auto& header_rules = config_->headerRules();
  for (int i = 0; i < header_rules.size(); ++i) {
    const auto& rule = header_rules[i];
    const auto& state = header_rule_states_[i];

    if (!state.ever_matched && rule.has_on_missing()) {
      const auto& on_missing = rule.on_missing();
      ContentParser::MetadataAction action;
      action.namespace_ = on_missing.metadata_namespace().empty()
                              ? std::string(FilterConfig::DefaultHeaderNamespace)
                              : on_missing.metadata_namespace();
      action.key = on_missing.key();
      if (on_missing.has_value()) {
        action.value = on_missing.value();
      }

      if (addMetadata(action)) {
        config_->stats().metadata_added_.inc();
        config_->stats().metadata_from_fallback_.inc();
      }
    }
  }

  writeMetadata();
  processing_complete_ = true;
}

bool Filter::addMetadata(const ContentParser::MetadataAction& action) {
  const std::string& namespace_str = action.namespace_;

  // Check preserve_existing: check pending batch first, then committed metadata.
  if (action.preserve_existing) {
    // Check pending batch.
    const auto pending_it = structs_by_namespace_.find(namespace_str);
    if (pending_it != structs_by_namespace_.end() &&
        pending_it->second.fields().contains(action.key)) {
      ENVOY_LOG(trace, "Preserving existing metadata value for key {} in namespace {}", action.key,
                namespace_str);
      config_->stats().preserved_existing_metadata_.inc();
      return false;
    }
    // Check committed dynamic metadata.
    const auto& filter_metadata =
        encoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto entry_it = filter_metadata.find(namespace_str);
    if (entry_it != filter_metadata.end() && entry_it->second.fields().contains(action.key)) {
      ENVOY_LOG(trace, "Preserving existing metadata value for key {} in namespace {}", action.key,
                namespace_str);
      config_->stats().preserved_existing_metadata_.inc();
      return false;
    }
  }

  // Check if value is set.
  if (!action.value.has_value()) {
    ENVOY_LOG(warn, "No value to write for key {} in namespace {}", action.key, namespace_str);
    return false;
  }

  const Protobuf::Value& proto_value = action.value.value();

  // Check if the Protobuf::Value has a valid type set.
  if (proto_value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    ENVOY_LOG(warn, "Value type conversion failed for key {} in namespace {}", action.key,
              namespace_str);
    config_->stats().type_conversion_error_.inc();
    return false;
  }

  // Accumulate into pending batch.
  (*structs_by_namespace_[namespace_str].mutable_fields())[action.key] = proto_value;

  ENVOY_LOG(trace, "Added pending metadata: namespace={}, key={}", namespace_str, action.key);
  return true;
}

void Filter::writeMetadata() {
  for (const auto& entry : structs_by_namespace_) {
    encoder_callbacks_->streamInfo().setDynamicMetadata(entry.first, entry.second);
  }
  structs_by_namespace_.clear();
}

bool Filter::allHeaderRulesSatisfied() const {
  const auto& rules = config_->headerRules();
  for (int i = 0; i < rules.size(); ++i) {
    if (rules[i].stop_processing_after_matches() == 0) {
      return false;
    }
    if (header_rule_states_[i].match_count < rules[i].stop_processing_after_matches()) {
      return false;
    }
  }
  return true;
}

} // namespace AwsEventstreamParser
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
