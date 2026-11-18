#include "source/extensions/filters/http/body_size_limit/body_size_limit_filter.h"

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/http/codes.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

BodySizeLimitFilterConfig::BodySizeLimitFilterConfig(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config,
    Stats::Scope& scope, const std::string& stats_prefix)
    : max_request_bytes_(proto_config.has_max_request_bytes()
                             ? static_cast<uint64_t>(proto_config.max_request_bytes().value())
                             : 0),
      stats_{ALL_BODY_SIZE_LIMIT_STATS(
          POOL_COUNTER_PREFIX(scope, stats_prefix + "body_size_limit."))} {}

BodySizeLimitFilter::BodySizeLimitFilter(BodySizeLimitFilterConfigSharedPtr config)
    : config_(config) {}

Http::FilterHeadersStatus BodySizeLimitFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                             bool end_stream) {
  if (end_stream) {
    // Header-only request, nothing to limit.
    return Http::FilterHeadersStatus::Continue;
  }

  if (config_->maxRequestBytes() == 0) {
    return Http::FilterHeadersStatus::Continue;
  }

  // If Content-Length is present and exceeds the limit, reject immediately.
  auto cl = headers.getContentLengthValue();
  uint64_t content_length = 0;
  if (!cl.empty() && absl::SimpleAtoi(cl, &content_length)) {
    if (content_length > config_->maxRequestBytes()) {
      config_->stats().rq_content_length_too_large_.inc();
      sizeExceeded(content_length, "content length exceeds maximum", "content length too large");
      return Http::FilterHeadersStatus::StopIteration;
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus BodySizeLimitFilter::decodeData(Buffer::Instance& data, bool) {
  if (config_->maxRequestBytes() == 0) {
    return Http::FilterDataStatus::Continue;
  }

  bytes_received_ += data.length();
  if (bytes_received_ > config_->maxRequestBytes()) {
    config_->stats().rq_body_too_large_.inc();
    sizeExceeded(bytes_received_, "request size exceeds maximum", "request too long");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

void BodySizeLimitFilter::sizeExceeded(uint64_t length, const char* log_message,
                                       const char* reply_text) {
  // A rejection is client-controlled, so it is logged per stream at debug rather than at error:
  // the counters and the access log's response code details record it without letting a client
  // drive the error log.
  ENVOY_STREAM_LOG(debug, "{} ({} > {})", *decoder_callbacks_, log_message, length,
                   config_->maxRequestBytes());

  decoder_callbacks_->sendLocalReply(Http::Code::PayloadTooLarge, reply_text, nullptr, std::nullopt,
                                     StreamInfo::ResponseCodeDetails::get().RequestPayloadTooLarge);
}

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
