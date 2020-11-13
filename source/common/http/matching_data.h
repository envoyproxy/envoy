#pragma once

#include "envoy/http/header_map.h"
#include "envoy/buffer/buffer.h"
#include "common/buffer/buffer_impl.h"
#include <memory>

namespace Envoy {
namespace Http {

struct HttpMatchingData {
public:
  void onRequestHeaders(Http::RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
  }

  void onRequestData(const Buffer::Instance& buffer) {
    if (!request_buffer_copy_) {
      request_buffer_copy_ = std::make_unique<Buffer::OwnedImpl>();
    }
    // TODO(snowp): Instead of copying the data, we should instead just use the FM buffer and store
    // a pointer to that here. This would require the match result to influence flow control.
    request_buffer_copy_->add(buffer);
  }

  void onRequestTrailers(Http::RequestTrailerMap& request_trailers) {
    request_trailers_ = &request_trailers;
  }

  void onResponseHeaders(Http::ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
  }

  void onResponseData(const Buffer::Instance& buffer) {
    if (!response_buffer_copy_) {
      response_buffer_copy_ = std::make_unique<Buffer::OwnedImpl>();
    }
    // TODO(snowp): Instead of copying the data, we should instead just use the FM buffer and store
    // a pointer to that here. This would require the match result to influence flow control.
    response_buffer_copy_->add(buffer);
  }

  void onResponseTrailers(Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
  }

  Http::RequestHeaderMap* request_headers_{};
  Http::RequestTrailerMap* request_trailers_{};
  Http::ResponseHeaderMap* response_headers_{};
  Http::ResponseTrailerMap* response_trailers_{};

  Buffer::InstancePtr request_buffer_copy_;
  Buffer::InstancePtr response_buffer_copy_;
};
using HttpMatchingDataSharedPtr = std::shared_ptr<HttpMatchingData>;

} // namespace Http
} // namespace Envoy