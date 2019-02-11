#include "extensions/filters/http/tap/tap_config_impl.h"

#include "envoy/data/tap/v2alpha/http.pb.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TapFilter {

HttpTapConfigImpl::HttpTapConfigImpl(envoy::service::tap::v2alpha::TapConfig&& proto_config,
                                     Common::Tap::Sink* admin_streamer)
    : Extensions::Common::Tap::TapConfigBaseImpl(std::move(proto_config), admin_streamer) {}

HttpPerRequestTapperPtr HttpTapConfigImpl::createPerRequestTapper(uint64_t stream_id) {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this(), stream_id);
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::HeaderMap& headers) {
  config_->rootMatcher().onHttpRequestHeaders(headers, statuses_);
}

void HttpPerRequestTapperImpl::onRequestBody(const Buffer::Instance& data) {
  Extensions::Common::Tap::Utility::addBufferToProtoBytes(
      *trace_->mutable_http_buffered_trace()->mutable_request()->mutable_body(),
      config_->maxBufferedRxBytes(), data);
}

void HttpPerRequestTapperImpl::onRequestTrailers(const Http::HeaderMap& trailers) {
  config_->rootMatcher().onHttpRequestTrailers(trailers, statuses_);
}

void HttpPerRequestTapperImpl::onResponseHeaders(const Http::HeaderMap& headers) {
  config_->rootMatcher().onHttpResponseHeaders(headers, statuses_);
}

void HttpPerRequestTapperImpl::onResponseBody(const Buffer::Instance& data) {
  Extensions::Common::Tap::Utility::addBufferToProtoBytes(
      *trace_->mutable_http_buffered_trace()->mutable_response()->mutable_body(),
      config_->maxBufferedTxBytes(), data);
}

void HttpPerRequestTapperImpl::onResponseTrailers(const Http::HeaderMap& trailers) {
  config_->rootMatcher().onHttpResponseTrailers(trailers, statuses_);
}

namespace {
Http::HeaderMap::Iterate fillHeaderList(const Http::HeaderEntry& header, void* context) {
  Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>& header_list =
      *reinterpret_cast<Protobuf::RepeatedPtrField<envoy::api::v2::core::HeaderValue>*>(context);
  auto& new_header = *header_list.Add();
  new_header.set_key(header.key().c_str());
  new_header.set_value(header.value().c_str());
  return Http::HeaderMap::Iterate::Continue;
}
} // namespace

bool HttpPerRequestTapperImpl::onDestroyLog(const Http::HeaderMap* request_headers,
                                            const Http::HeaderMap* request_trailers,
                                            const Http::HeaderMap* response_headers,
                                            const Http::HeaderMap* response_trailers) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return false;
  }

  auto& http_trace = *trace_->mutable_http_buffered_trace();
  request_headers->iterate(fillHeaderList, http_trace.mutable_request()->mutable_headers());
  if (request_trailers != nullptr) {
    request_trailers->iterate(fillHeaderList, http_trace.mutable_request()->mutable_trailers());
  }
  if (response_headers != nullptr) {
    response_headers->iterate(fillHeaderList, http_trace.mutable_response()->mutable_headers());
  }
  if (response_trailers != nullptr) {
    response_trailers->iterate(fillHeaderList, http_trace.mutable_response()->mutable_trailers());
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  config_->submitBufferedTrace(trace_, stream_id_);
  return true;
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
