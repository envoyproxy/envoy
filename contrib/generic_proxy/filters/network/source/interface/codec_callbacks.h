#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Callbacks of ServerCodec.
 */
class ServerCodecCallbacks {
public:
  virtual ~ServerCodecCallbacks() = default;
  /**
   * If request decoding success then this method will be called.
   * @param frame request frame from decoding. Frist frame should be StreamRequest
   * frame.
   * NOTE: This method will be called multiple times for the multiple frames request.
   * FrameFlags and embedded StreamFlags could be used to correlate frames of same
   * request.
   */
  virtual void onDecodingSuccess(StreamFramePtr frame) PURE;

  /**
   * If request decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;

  /**
   * Write specified data to the downstream connection. This is could be used to write
   * some raw binary to peer before the onDecodingSuccess()/onDecodingFailure() is
   * called. By this way, when some special data is received from peer, the custom
   * codec could handle it directly and write some reply to peer without notifying
   * the generic proxy filter.
   * @param buffer data to write.
   */
  virtual void writeToConnection(Buffer::Instance& buffer) PURE;

  /**
   * @return the downstream connection that the request is received from. This gives
   * the custom codec the full power to control the downstream connection.
   */
  virtual OptRef<Network::Connection> connection() PURE;
};

/**
 * Callbacks of ClientCodec.
 */
class ClientCodecCallbacks {
public:
  virtual ~ClientCodecCallbacks() = default;

  /**
   * If response decoding success then this method will be called.
   * @param frame response frame from decoding. Frist frame should be StreamResponse
   * frame.
   * NOTE: This method will be called multiple times for the multiple frames response.
   * FrameFlags and embedded StreamFlags could be used to correlate frames of same
   * request. And the StreamFlags could also be used to correlate the response with
   * the request.
   */
  virtual void onDecodingSuccess(StreamFramePtr frame) PURE;

  /**
   * If response decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;

  /**
   * Write specified data to the upstream connection. This is could be used to write
   * some raw binary to peer before the onDecodingSuccess()/onDecodingFailure() is
   * called. By this way, when some special data is received from peer, the custom
   * codec could handle it directly and write some reply to peer without notifying
   * the generic proxy filter.
   * @param buffer data to write.
   */
  virtual void writeToConnection(Buffer::Instance& buffer) PURE;

  /**
   * @return the upstream connection that the response is received from. This gives
   * the custom codec the full power to control the upstream connection.
   */
  virtual OptRef<Network::Connection> connection() PURE;
};

/**
 * Callback of request/response frame.
 */
class EncodingCallbacks {
public:
  virtual ~EncodingCallbacks() = default;

  /**
   * If encoding success then this method will be called to notify the generic proxy.
   * @param buffer encoding result buffer.
   * @param end_stream if last frame is encoded.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
