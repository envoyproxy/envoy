#include "test/common/http/filters/test_remote_response/filter.h"

#include "envoy/http/header_map.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

#include "library/common/http/header_utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestRemoteResponse {

Http::FilterHeadersStatus TestRemoteResponseFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                  bool end_stream) {
  headers_ = &headers;
  if (end_stream) {
    sendResponse();
  }
  return Http::FilterHeadersStatus::StopIteration;
}
Http::FilterDataStatus TestRemoteResponseFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    sendResponse();
  }
  return Http::FilterDataStatus::StopIterationAndBuffer;
}
Http::FilterTrailersStatus TestRemoteResponseFilter::decodeTrailers(Http::RequestTrailerMap&) {
  sendResponse();
  return Http::FilterTrailersStatus::StopIteration;
}

void TestRemoteResponseFilter::sendResponse() {
  std::cerr << *headers_;
  bool send_trailers = !headers_->get(Http::LowerCaseString("send-trailers")).empty();

  Http::ResponseHeaderMapPtr headers{
      Http::createHeaderMap<Http::ResponseHeaderMapImpl>({{Http::Headers::get().Status, "200"}})};
  decoder_callbacks_->encodeHeaders(std::move(headers), false,
                                    StreamInfo::ResponseCodeDetails::get().ViaUpstream);
  Buffer::OwnedImpl body("data");
  decoder_callbacks_->encodeData(body, !send_trailers);
  if (send_trailers) {
    std::string trailers_value(
        headers_->get(Http::LowerCaseString("send-trailers"))[0]->value().getStringView());
    Http::ResponseTrailerMapPtr trailers;
    if (trailers_value == "empty") {
      trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>({});
    } else if (trailers_value == "empty-value") {
      trailers =
          Http::createHeaderMap<Http::ResponseTrailerMapImpl>({{Http::LowerCaseString("foo"), ""}});
    } else {
      trailers = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(
          {{Http::LowerCaseString("foo"), "bar"}});
    }
    decoder_callbacks_->encodeTrailers(std::move(trailers));
  }
}

} // namespace TestRemoteResponse
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
