#pragma once

#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"

#include "library/common/types/c_types.h"

namespace Envoy {
class InternalEngine;
namespace Platform {

class Stream {
public:
  Stream(InternalEngine* engine, envoy_stream_t handle);

  /**
   * Send the headers over an open HTTP stream. This function can be invoked
   * once and needs to be called before `sendData`.
   *
   * @param headers the headers to send.
   * @param end_stream indicates whether to close the stream locally after sending this frame.
   * @param idempotent indicates that the request is idempotent. When idempotent is set to true
   *                   Envoy Mobile will retry on HTTP/3 post-handshake failures. By default, it is
   *                   set to false.
   */
  Stream& sendHeaders(Http::RequestHeaderMapPtr headers, bool end_stream, bool idempotent = false);

  /**
   * Send data over an open HTTP stream. This method can be invoked multiple times.
   *
   * @param buffer the data to send.
   */
  Stream& sendData(Buffer::InstancePtr buffer);

  /**
   * Reads the data up to the number of `bytes_to_read`.
   *
   * @param bytes_to_read the number of bytes read
   */
  Stream& readData(size_t bytes_to_read);

  /**
   * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
   * Note that this method implicitly closes the stream locally.
   *
   * @param trailers the trailers to send.send
   */
  void close(Http::RequestTrailerMapPtr trailers);

  /**
   * Send data over an open HTTP stream and closes the stream.. This method can only be invoked
   * once.
   *
   * @param buffer the last data to send.
   */
  void close(Buffer::InstancePtr buffer);

  /** Cancels the stream. */
  void cancel();

private:
  InternalEngine* engine_;
  envoy_stream_t handle_;
};

using StreamSharedPtr = std::shared_ptr<Stream>;

} // namespace Platform
} // namespace Envoy
