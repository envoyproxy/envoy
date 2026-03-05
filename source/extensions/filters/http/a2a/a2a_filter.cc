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
  // TODO(tyxia): Only support GET and POST for now. Investigate if other methods should be
  // supported in the future.
  if (isValidA2aGetRequest(headers)) {
    is_a2a_request_ = true;
    ENVOY_LOG(debug, "valid A2A GET request, passing through");
    return Http::FilterHeadersStatus::Continue;
  }

  if (isValidA2aPostRequest(headers)) {
    is_json_post_request_ = true;
    ENVOY_LOG(debug, "valid A2A POST request");
    if (end_stream) {
      is_a2a_request_ = false;
    } else {
      // TODO(tyxia) Set the max request body size limit, depends on the way of handling the body
      // data in decodeData.
      is_a2a_request_ = true;

      // Stop iteration to validate the body in decodeData (e.g., validate JSON-RPC 2.0).
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

  // TODO(tyxia) Handle the data parsing.
  return Http::FilterDataStatus::Continue;
}

} // namespace A2a
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
