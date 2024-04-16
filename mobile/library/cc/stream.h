#pragma once

#include <vector>

#include "envoy/http/header_map.h"

#include "library/cc/request_headers.h"
#include "library/cc/request_trailers.h"
#include "library/common/types/c_types.h"

namespace Envoy {
class InternalEngine;
namespace Platform {

class Stream {
public:
  Stream(InternalEngine* engine, envoy_stream_t handle);

  [[deprecated]] Stream& sendHeaders(RequestHeadersSharedPtr headers, bool end_stream);

  /**
   * Send the headers over an open HTTP stream. This function can be invoked
   * once and needs to be called before `sendData`.
   *
   * @param headers the headers to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   */
  Stream& sendHeaders(Http::RequestHeaderMapPtr headers, bool end_stream);

  Stream& sendData(envoy_data data);

  Stream& readData(size_t bytes_to_read);

  [[deprecated]] void close(RequestTrailersSharedPtr trailers);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   *
   * @param trailers the trailers to send.send
   */
  void close(Http::RequestTrailerMapPtr trailers);

  void close(envoy_data data);

  void cancel();

private:
  InternalEngine* engine_;
  envoy_stream_t handle_;
};

using StreamSharedPtr = std::shared_ptr<Stream>;

} // namespace Platform
} // namespace Envoy
