#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "common/buffer/buffer_impl.h"

namespace Envoy {
namespace Http {

struct HttpMatchingData {
public:
  void onRequestHeaders(Http::RequestHeaderMap& request_headers) {
    request_headers_ = &request_headers;
  }

  void onRequestData(const Buffer::Instance& buffer, bool end_stream) {
    if (!request_buffer_copy_) {
      request_buffer_copy_ = std::make_unique<Buffer::OwnedImpl>();
    }
    // TODO(snowp): Instead of copying the data, we should instead just use the FM buffer and store
    // a pointer to that here. This would require the match result to influence flow control.
    request_buffer_copy_->add(buffer);

    request_end_stream_ = end_stream;
  }

  void onRequestTrailers(Http::RequestTrailerMap& request_trailers) {
    request_trailers_ = &request_trailers;
    request_end_stream_ = true;
  }

  void onResponseHeaders(Http::ResponseHeaderMap& response_headers) {
    response_headers_ = &response_headers;
  }

  void onResponseData(const Buffer::Instance& buffer, bool end_stream) {
    if (!response_buffer_copy_) {
      response_buffer_copy_ = std::make_unique<Buffer::OwnedImpl>();
    }
    // TODO(snowp): Instead of copying the data, we should instead just use the FM buffer and store
    // a pointer to that here. This would require the match result to influence flow control.
    response_buffer_copy_->add(buffer);

    response_end_stream_ = end_stream;
  }

  void onResponseTrailers(Http::ResponseTrailerMap& response_trailers) {
    response_trailers_ = &response_trailers;
    response_end_stream_ = true;
  }

  Http::RequestHeaderMap* request_headers_{};
  Http::RequestTrailerMap* request_trailers_{};
  Http::ResponseHeaderMap* response_headers_{};
  Http::ResponseTrailerMap* response_trailers_{};

  Buffer::InstancePtr request_buffer_copy_;
  Buffer::InstancePtr response_buffer_copy_;

  bool request_end_stream_{};
  bool response_end_stream_{};
};
using HttpMatchingDataSharedPtr = std::shared_ptr<HttpMatchingData>;

} // namespace Http
} // namespace Envoy