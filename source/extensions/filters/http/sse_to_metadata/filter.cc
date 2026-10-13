#include "source/extensions/filters/http/sse_to_metadata/filter.h"

#include <functional>
#include <string>

#include "source/common/common/utility.h"
#include "source/common/config/utility.h"
#include "source/common/http/sse/sse_parser.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/strip.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SseToMetadata {

namespace {
constexpr absl::string_view SseContentType{"text/event-stream"};

// Check if content type is text/event-stream, ignoring parameters like charset.
// HTTP Content-Type is case-insensitive.
bool isSseContentType(absl::string_view content_type) {
  absl::string_view normalized = StringUtil::trim(StringUtil::cropRight(content_type, ";"));
  return absl::EqualsIgnoreCase(normalized, SseContentType);
}

} // namespace

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::sse_to_metadata::v3::SseToMetadata& config,
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
      stats_(generateStats(fmt::format("sse_to_metadata.resp.{}", parser_factory_->statsPrefix()),
                           context.scope())),
      max_event_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(config.response_rules(), max_event_size, 8192)) {}

Filter::Filter(std::shared_ptr<FilterConfig> config)
    : config_(std::move(config)), parser_(config_->parserFactory().createParser()) {}

Http::FilterHeadersStatus Filter::encodeHeaders(Http::ResponseHeaderMap& headers, bool) {
  absl::string_view content_type = headers.getContentTypeValue();
  if (!content_type.empty() && isSseContentType(content_type)) {
    content_type_matched_ = true;
  } else {
    if (content_type.empty()) {
      ENVOY_LOG(trace, "Missing Content-Type header (SSE streams require text/event-stream)");
    } else {
      ENVOY_LOG(trace, "Content-Type '{}' is not text/event-stream", content_type);
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
    processBuffer(end_stream);
  }

  // Finalize rules at end of stream or when processing stopped early
  if (end_stream || processing_complete_) {
    finalizeRules();
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus Filter::encodeTrailers(Http::ResponseTrailerMap&) {
  // Finalize rules if not already done (only if Content-Type matched)
  if (content_type_matched_ && !processing_complete_) {
    finalizeRules();
  }
  return Http::FilterTrailersStatus::Continue;
}

void Filter::processBuffer(bool end_stream) {
  // Linearize buffer to get contiguous memory for string_view.
  const uint64_t length = buffer_.length();
  absl::string_view buffer_view(static_cast<const char*>(buffer_.linearize(length)), length);

  while (!buffer_view.empty() && !processing_complete_) {
    auto result = Http::Sse::SseParser::findEventEnd(buffer_view, end_stream);

    if (result.event_start == absl::string_view::npos) {
      // No complete event found. Check if buffer exceeds max size.
      const uint32_t max_size = config_->maxEventSize();
      if (max_size > 0 && buffer_view.size() > max_size) {
        ENVOY_LOG(
            warn,
            "SSE event exceeds max_event_size ({} bytes). Discarding {} bytes of buffered data.",
            max_size, buffer_view.size());
        config_->stats().event_too_large_.inc();
        buffer_.drain(buffer_.length());
        return;
      }
      break;
    }

    absl::string_view event =
        buffer_view.substr(result.event_start, result.event_end - result.event_start);
    ENVOY_LOG(trace, "Processing SSE event: {}", absl::CEscape(event));

    if (processSseEvent(event)) {
      processing_complete_ = true;
      break;
    }

    buffer_view = buffer_view.substr(result.next_start);
  }

  // Drain processed bytes from the front of the buffer.
  buffer_.drain(length - buffer_view.size());
}

bool Filter::processSseEvent(absl::string_view event) {
  auto parsed_event = Http::Sse::SseParser::parseEvent(event);

  if (!parsed_event.data.has_value() || parsed_event.data.value().empty()) {
    ENVOY_LOG(debug, "Event does not contain 'data' field");
    config_->stats().no_data_field_.inc();
    return false;
  }

  auto result = parser_->parse(parsed_event.data.value());

  if (result.error_message.has_value()) {
    ENVOY_LOG(debug, "Parser reported error: {}", result.error_message.value());
    config_->stats().parse_error_.inc();
    return false;
  }

  // Execute immediate actions returned by parser
  for (const auto& action : result.immediate_actions) {
    if (writeMetadata(action)) {
      config_->stats().metadata_added_.inc();
    }
  }

  return result.stop_processing;
}

void Filter::finalizeRules() {
  // Get all deferred actions from parser (handles on_error/on_missing for unmatched rules)
  auto deferred_actions = parser_->getAllDeferredActions();

  if (!deferred_actions.empty()) {
    ENVOY_LOG(trace, "Executing {} deferred actions at end of stream", deferred_actions.size());
  }

  for (const auto& action : deferred_actions) {
    if (writeMetadata(action)) {
      config_->stats().metadata_added_.inc();
      config_->stats().metadata_from_fallback_.inc();
    }
  }

  processing_complete_ = true;
}

bool Filter::writeMetadata(const ContentParser::MetadataAction& action) {
  // Parser is responsible for applying namespace defaults.
  // If namespace is empty here, it's a parser implementation issue.
  const std::string& namespace_str = action.namespace_;

  // Check preserve_existing
  if (action.preserve_existing) {
    const auto& filter_metadata =
        encoder_callbacks_->streamInfo().dynamicMetadata().filter_metadata();
    const auto entry_it = filter_metadata.find(namespace_str);
    if (entry_it != filter_metadata.end()) {
      const auto& metadata = entry_it->second;
      if (metadata.fields().contains(action.key)) {
        ENVOY_LOG(trace, "Preserving existing metadata value for key {} in namespace {}",
                  action.key, namespace_str);
        config_->stats().preserved_existing_metadata_.inc();
        return false;
      }
    }
  }

  // Check if value is set
  if (!action.value.has_value()) {
    ENVOY_LOG(warn, "No value to write for key {} in namespace {}", action.key, namespace_str);
    return false;
  }

  // Write to dynamic metadata using the Protobuf::Value directly
  const Protobuf::Value& proto_value = action.value.value();

  // Check if the Protobuf::Value has a valid type set
  if (proto_value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    ENVOY_LOG(warn, "Value type conversion failed for key {} in namespace {}", action.key,
              namespace_str);
    return false;
  }

  Protobuf::Struct metadata;
  (*metadata.mutable_fields())[action.key] = proto_value;
  encoder_callbacks_->streamInfo().setDynamicMetadata(namespace_str, metadata);

  ENVOY_LOG(trace, "Wrote metadata: namespace={}, key={}", namespace_str, action.key);
  return true;
}

} // namespace SseToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
