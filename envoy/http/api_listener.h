#pragma once

#include "envoy/http/codec.h"

namespace Envoy {
namespace Http {

class RequestDecoderHandle {
public:
  virtual ~RequestDecoderHandle() = default;

  /**
   * @return a reference to the underlying decoder if it is still valid.
   */
  virtual OptRef<RequestDecoder> get() PURE;
};
using RequestDecoderHandlePtr = std::unique_ptr<RequestDecoderHandle>;

/**
 * ApiListener that allows consumers to interact with HTTP streams via API calls.
 */
// TODO(junr03): this is a replica of the functions in ServerConnectionCallbacks. It would be nice
// to not duplicate this interface layout.
class ApiListener {
public:
  virtual ~ApiListener() = default;

  /**
   * Invoked when a new request stream is initiated by the remote.
   * @param response_encoder supplies the encoder to use for creating the response. The request and
   *                         response are backed by the same Stream object.
   * @param is_internally_created indicates if this stream was originated by a
   *   client, or was created by Envoy, by example as part of an internal redirect.
   * @return RequestDecoderHandle supplies the decoder callbacks to fire into for stream
   *   decoding events.
   */
  virtual RequestDecoderHandlePtr newStreamHandle(ResponseEncoder& response_encoder,
                                                  bool is_internally_created = false) PURE;
};

using ApiListenerPtr = std::unique_ptr<ApiListener>;
using ApiListenerOptRef = absl::optional<std::reference_wrapper<ApiListener>>;

} // namespace Http
} // namespace Envoy
