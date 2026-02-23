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

Http::FilterDataStatus A2aFilter::decodeData(Buffer::Instance&, bool) {
  if (!is_json_post_request_ || !is_a2a_request_) {
    return Http::FilterDataStatus::Continue;
  }

  if (!parser_) {
    parser_ = std::make_unique<A2aJsonParser>(config_->parserConfig());
  }

  // TODO(tyxia) Handle the parsing data.
  return Http::FilterDataStatus::Continue;
}

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
