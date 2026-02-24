#include "source/extensions/filters/http/a2a/a2a_filter.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/utility.h"

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

bool A2aFilter::isValidA2aGetOrDeleteRequest(const Http::RequestHeaderMap& headers) const {
  return headers.getMethodValue() == Http::Headers::get().MethodValues.Get ||
         headers.getMethodValue() == Http::Headers::get().MethodValues.Delete;
}

bool A2aFilter::isValidA2aPostRequest(const Http::RequestHeaderMap& headers) const {
  if (headers.getMethodValue() != Http::Headers::get().MethodValues.Post) {
    return false;
  }

  // Extract Content-Type
  const absl::string_view content_type = headers.getContentTypeValue();
  const absl::string_view json_ct = Envoy::Http::Headers::get().ContentTypeValues.Json;

  // We check if it is exactly "application/json" or starts with "application/json;" /
  // "application/json "
  const bool is_json_content_type =
      absl::StartsWith(content_type, json_ct) &&
      (content_type.size() == json_ct.size() || content_type[json_ct.size()] == ';' ||
       content_type[json_ct.size()] == ' ');

  return is_json_content_type;
}

bool A2aFilter::shouldRejectRequest() const {
  return config_->trafficMode() == envoy::extensions::filters::http::a2a::v3::A2a::REJECT;
}

uint32_t A2aFilter::getMaxRequestBodySize() const { return config_->maxRequestBodySize(); }

Http::FilterHeadersStatus A2aFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                   bool end_stream) {
  if (isValidA2aGetOrDeleteRequest(headers)) {
    is_a2a_request_ = true;
    ENVOY_LOG(debug, "valid A2A GET/DELETE request, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  if (isValidA2aPostRequest(headers)) {
    is_json_post_request_ = true;
    ENVOY_LOG(debug, "valid A2A Post request");
    if (end_stream) {
      is_a2a_request_ = false;
    } else {
      // Need to buffer the body to check for JSON-RPC 2.0
      is_a2a_request_ = true;

      const uint32_t max_size = getMaxRequestBodySize();
      if (max_size > 0) {
        decoder_callbacks_->setDecoderBufferLimit(max_size);
        ENVOY_LOG(debug, "set decoder buffer limit to {} bytes", max_size);
      }

      // Stop iteration to buffer the body for validation
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
  if (!is_json_post_request_ || !is_a2a_request_) {
    return Http::FilterDataStatus::Continue;
  }

  if (parsing_complete_) {
    return Http::FilterDataStatus::Continue;
  }

  if (!parser_) {
    parser_ = std::make_unique<A2aJsonParser>(config_->parserConfig());
  }

  ENVOY_LOG(trace, "decodeData: buffer_size={}, already_parsed={}", data.length(), bytes_parsed_);

  const uint32_t max_size = getMaxRequestBodySize();
  if (max_size > 0 && bytes_parsed_ >= max_size) {
    config_->stats().body_too_large_.inc();
    handleParseError("request body is too large.");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  uint64_t bytes_to_skip = bytes_parsed_;

  for (const Buffer::RawSlice& slice : data.getRawSlices()) {
    if (bytes_to_skip >= slice.len_) {
      bytes_to_skip -= slice.len_;
      continue;
    }

    // This slice contains the "new" data
    const char* start = static_cast<const char*>(slice.mem_) + bytes_to_skip;
    size_t len = slice.len_ - bytes_to_skip;

    // Once we've skipped the initial 'bytes_parsed_',
    // we process all remaining bytes in this and future slices.
    bytes_to_skip = 0;

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
        decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, "not a valid JSON", nullptr,
                                           absl::nullopt, "a2a_filter_not_jsonrpc");
        return Http::FilterDataStatus::StopIterationNoBuffer;
      }
    }

    if (max_size > 0 && bytes_parsed_ >= max_size)
      break;
  }

  // If we are here, we haven't collected all fields yet.
  bool size_limit_hit = (max_size > 0 && bytes_parsed_ >= max_size);
  if (end_stream || size_limit_hit) {
    auto final_status = parser_->finishParse();
    if (!final_status.ok()) {
      config_->stats().body_too_large_.inc();
      handleParseError("reached end_stream or configured body size, don't get enough data.");
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
                                       nullptr, absl::nullopt, "a2a_filter_not_jsonrpc");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
