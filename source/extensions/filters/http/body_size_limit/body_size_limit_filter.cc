#include "source/extensions/filters/http/body_size_limit/body_size_limit_filter.h"

#include "envoy/extensions/filters/http/body_size_limit/v3/body_size_limit.pb.h"
#include "envoy/http/codes.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BodySizeLimitFilter {

BodySizeLimitFilterConfig::BodySizeLimitFilterConfig(
    const envoy::extensions::filters::http::body_size_limit::v3::BodySizeLimit& proto_config)
    : max_request_bytes_(proto_config.has_max_request_bytes()
                             ? static_cast<uint64_t>(proto_config.max_request_bytes().value())
                             : 0) {}

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
  int64_t content_length;
  if (!cl.empty() && absl::SimpleAtoi(cl, &content_length)) {
    if (content_length > static_cast<int64_t>(config_->maxRequestBytes())) {
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
    sizeExceeded(bytes_received_, "request size exceeds maximum", "request too long");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus BodySizeLimitFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::Continue;
}

void BodySizeLimitFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void BodySizeLimitFilter::sizeExceeded(uint64_t length, const char* logMessage,
                                       const char* replyText) {
  const auto& provider = callbacks_->streamInfo().downstreamAddressProvider();
  const auto& client_addr = provider.remoteAddress(); // Network::Address::Instance
  std::string client_address;
  if (client_addr && client_addr->ip()) {
    client_address =
        client_addr->ip()->addressAsString() + ":" + std::to_string(client_addr->ip()->port());
  }

  std::string path("/");
  auto headers = callbacks_->requestHeaders();
  if (headers) {
    path = headers->getPathValue();
  }

  auto upstream_info = callbacks_->streamInfo().upstreamInfo();
  std::string target_host;
  if (upstream_info && upstream_info->upstreamHost() && upstream_info->upstreamHost()->address()) {
    target_host = upstream_info->upstreamHost()->address()->asString();
  }

  ENVOY_LOG(error, "{} ({} > {}), client: {}, target: {}{}", logMessage, length,
            static_cast<int64_t>(config_->maxRequestBytes()), client_address.c_str(), target_host,
            path.c_str());

  callbacks_->sendLocalReply(Http::Code::PayloadTooLarge, replyText, nullptr, std::nullopt,
                             logMessage);
}

} // namespace BodySizeLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
