#pragma once

#include "envoy/http/header_map.h"
#include "envoy/buffer/buffer.h"

namespace Envoy {
namespace Http {

struct HttpMatchingData {
public:
  void onRequestHeaders(Http::RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
  }

  void onRequestData(const Buffer::Instance&) {
    // TODO(snowp): ???
  }

  void onRequestTrailers(Http::RequestTrailerMap& request_trailers) {
    request_trailers_ = &request_trailers;
  }

  void onResponseHeaders(Http::ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
  }

  void onResponseData(const Buffer::Instance&) {
    // TODO(snowp): ???
  }

  void onResponseTrailers(Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
  }

  Http::RequestHeaderMap* request_headers_{};
  Http::RequestTrailerMap* request_trailers_{};
  Http::ResponseHeaderMap* response_headers_{};
  Http::ResponseTrailerMap* response_trailers_{};
};
using HttpMatchingDataSharedPtr = std::shared_ptr<HttpMatchingData>;

} // namespace Http
} // namespace Envoy