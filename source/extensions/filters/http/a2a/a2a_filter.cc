#include "source/extensions/filters/http/a2a/a2a_filter.h"

#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace A2a {

namespace {
A2aFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, "a2a.");
  return A2aFilterStats{A2A_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}
} // namespace

A2aFilterConfig::A2aFilterConfig(const envoy::extensions::filters::http::a2a::v3::A2a& proto_config,
                                 const std::string& stats_prefix, Stats::Scope& scope)
    : traffic_mode_(proto_config.traffic_mode()),
      max_request_body_size_(
          PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, max_request_body_size, 8192)),
      parser_config_(A2aParserConfig::createDefault()), stats_(generateStats(stats_prefix, scope)) {
}

// A2A support three discovery strategies with GET requests: 1) Well-Known URI 2) Curated Registries
// 3) Private Discovery
// Well-Known URI is recommended for public agents or agents intended for broad discovery
// within a specific domain
// See: https://a2a-protocol.org/latest/topics/agent-discovery/#discovery-strategies
bool A2aFilter::isValidA2aGetRequest(const Http::RequestHeaderMap& headers) const {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Get;
}

bool A2aFilter::isValidA2aPostRequest(const Http::RequestHeaderMap& headers) const {
  if (headers.getMethodValue() != Http::Headers::get().MethodValues.Post) {
    return false;
  }

  // Extract Content-Type
  const absl::string_view content_type = headers.getContentTypeValue();
  const absl::string_view json_content_type = Envoy::Http::Headers::get().ContentTypeValues.Json;
  constexpr absl::string_view a2a_IANA_media_type = "application/a2a+json";

  const auto is_content_type_valid = [&](absl::string_view valid_ct) {
    return absl::StartsWith(content_type, valid_ct) &&
           (content_type.size() == valid_ct.size() || content_type[valid_ct.size()] == ';' ||
            content_type[valid_ct.size()] == ' ');
  };

  return is_content_type_valid(json_content_type) || is_content_type_valid(a2a_IANA_media_type);
}

bool A2aFilter::shouldRejectRequest() const {
  return config_->trafficMode() == envoy::extensions::filters::http::a2a::v3::A2a::REJECT;
}

uint32_t A2aFilter::getMaxRequestBodySize() const { return config_->maxRequestBodySize(); }

Http::FilterHeadersStatus A2aFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                   bool end_stream) {
  // According to RFC 7231, a payload body in a GET request has no defined semantics.
  // In A2A filter here, GET with body will be rejected in REJECT mode, will pass through in
  // PASS_THROUGH mode.
  if (isValidA2aGetRequest(headers) && end_stream) {
    is_a2a_request_ = true;
    ENVOY_LOG(debug, "valid A2A GET request, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  if (isValidA2aPostRequest(headers)) {
    if (end_stream) {
      is_a2a_request_ = false;
    } else {
      // Set it to true first to perform the JSON-RPC 2.0 compliance check in decodeData() phase.
      is_a2a_request_ = true;
      // Set the buffer limit
      const uint32_t max_size = getMaxRequestBodySize();
      if (max_size > 0) {
        decoder_callbacks_->setDecoderBufferLimit(max_size);
        ENVOY_LOG(debug, "set decoder buffer limit to {} bytes", max_size);
      }

      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  if (!is_a2a_request_ && shouldRejectRequest()) {
    ENVOY_LOG(debug, "rejecting non-A2A traffic");
    config_->stats().requests_rejected_.inc();
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "Only A2A traffic is allowed",
                                       nullptr, absl::nullopt, "a2a_filter_reject");
    return Http::FilterHeadersStatus::StopIteration;
  }

  ENVOY_LOG(debug, "A2A filter passing through during decoding headers");
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus A2aFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  if (!is_a2a_request_) {
    return Http::FilterDataStatus::Continue;
  }

  // Early return if we have already completed parsing.
  if (parsing_complete_) {
    return Http::FilterDataStatus::Continue;
  }

  if (!parser_) {
    parser_ = std::make_unique<A2aJsonParser>(config_->parserConfig());
  }

  ENVOY_LOG(trace, "decodeData: buffer_size={}, already_parsed={}, end_stream={}", data.length(),
            bytes_parsed_, end_stream);

  const uint32_t max_size = getMaxRequestBodySize();

  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    const char* start = static_cast<const char*>(slice.mem_);
    size_t len = slice.len_;

    if (max_size > 0) {
      len = std::min(len, static_cast<size_t>(max_size - bytes_parsed_));
    }

    if (len > 0) {
      auto status = parser_->parse({start, len});
      bytes_parsed_ += len;

      if (parser_->isAllFieldsCollected()) {
        ENVOY_LOG(debug, "a2a early parse termination: found all fields");
        return completeParsing();
      }

      if (!status.ok()) {
        config_->stats().invalid_json_.inc();
        decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "not a valid JSON", nullptr,
                                           absl::nullopt, "a2a_filter_not_valid_jsonrpc");
        return Http::FilterDataStatus::StopIterationNoBuffer;
      }
    }

    if (max_size > 0 && bytes_parsed_ == max_size) {
      break;
    }
  }

  // If we are here, we haven't collected all fields yet.
  bool size_limit_hit = (max_size > 0 && bytes_parsed_ == max_size);
  if (end_stream || size_limit_hit) {
    auto final_status = parser_->finishParse();
    if (!final_status.ok()) {
      // TODO(tyxia) Support the case that size limit hit before optional fields.
      if (size_limit_hit) {
        config_->stats().body_too_large_.inc();
        handleParseError("request body is too large.");
      } else {
        config_->stats().invalid_json_.inc();
        handleParseError("not a valid JSON (incomplete).");
      }
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    return completeParsing();
  }

  return Http::FilterDataStatus::StopIterationAndWatermark;
}

void A2aFilter::handleParseError(absl::string_view error_msg) {
  ENVOY_LOG(debug, "parse error: {}", error_msg);
  is_a2a_request_ = false;
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, error_msg, nullptr, absl::nullopt,
                                     "a2a_filter_parse_error");
}

Http::FilterDataStatus A2aFilter::completeParsing() {
  parsing_complete_ = true;
  is_a2a_request_ = parser_->isValidA2aRequest();

  ENVOY_LOG(debug, "parsing complete: is_a2a={}, bytes_parsed={}", is_a2a_request_, bytes_parsed_);

  if (!is_a2a_request_ && shouldRejectRequest()) {
    decoder_callbacks_->sendLocalReply(Http::Code::BadRequest,
                                       "request must be a valid JSON-RPC 2.0 message for A2A",
                                       nullptr, absl::nullopt, "a2a_filter_not_valid_jsonrpc");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  const auto& metadata = parser_->metadata();
  if (!metadata.fields().empty()) {
    // TODO(tyxia): Use the filter config name from the config for now. It can be customized and
    // controlled by the configuration. Also, the behavior of setting dynamic metadata can be
    // controlled by the configuration.
    decoder_callbacks_->streamInfo().setDynamicMetadata(
        std::string(decoder_callbacks_->filterConfigName()), metadata);
    ENVOY_LOG(debug, "A2A filter set dynamic metadata: {}", metadata.DebugString());
  }

  return Http::FilterDataStatus::Continue;
}

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
