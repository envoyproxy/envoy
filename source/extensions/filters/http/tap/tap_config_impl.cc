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

HttpPerRequestTapperPtr HttpTapConfigImpl::createPerRequestTapper() {
  return std::make_unique<HttpPerRequestTapperImpl>(shared_from_this());
}

void HttpPerRequestTapperImpl::onRequestHeaders(const Http::HeaderMap& headers) {
  config_->rootMatcher().updateMatchStatus(&headers, nullptr, statuses_);
}

void HttpPerRequestTapperImpl::onResponseHeaders(const Http::HeaderMap& headers) {
  config_->rootMatcher().updateMatchStatus(nullptr, &headers, statuses_);
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
                                            const Http::HeaderMap* response_headers) {
  if (!config_->rootMatcher().matches(statuses_)) {
    return false;
  }

  auto trace = std::make_shared<envoy::data::tap::v2alpha::BufferedTraceWrapper>();
  auto& http_trace = *trace->mutable_http_buffered_trace();
  request_headers->iterate(fillHeaderList, http_trace.mutable_request_headers());
  if (response_headers != nullptr) {
    response_headers->iterate(fillHeaderList, http_trace.mutable_response_headers());
  }

  ENVOY_LOG(debug, "submitting buffered trace sink");
  config_->sink().submitBufferedTrace(trace);
  return true;
}

} // namespace TapFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
