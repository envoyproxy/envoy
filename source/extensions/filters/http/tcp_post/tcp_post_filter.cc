#include "extensions/filters/http/tcp_post/tcp_post_filter.h"

#include <csignal>

#include "common/http/header_utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {

Http::FilterHeadersStatus TcpPostFilter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {
  // Only try to convert a POST request.
  if (headers.getMethodValue() == Envoy::Http::Headers::get().MethodValues.Post) {
    auto header_match_data = Http::HeaderUtility::buildHeaderDataVector(config_.headers());

    // Only trigger the conversion when headers are matched.
    if (Http::HeaderUtility::matchHeaders(headers, header_match_data)) {
      headers.setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Connect);
      headers.setProtocol(Envoy::Http::Headers::get().ProtocolValues.Bytestream);
    }
  }

  return Http::FilterHeadersStatus::Continue;
}

} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
