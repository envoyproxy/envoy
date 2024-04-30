#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"

#include "contrib/generic_proxy/filters/network/source/interface/route.h"
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
   * @param reason the reason of decoding failure.
   */
  virtual void onDecodingFailure(absl::string_view reason = {}) PURE;

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
   * @param reason the reason of decoding failure.
   */
  virtual void onDecodingFailure(absl::string_view reason = {}) PURE;

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

  /**
   * @return the upstream cluster that the request sent to and the response is received
   * from. The gives the custom codec the power to customize the behavior based on the
   * upstream cluster information.
   * This return value will be nullptr iff the whole callbacks is invalid.
   */
  virtual OptRef<const Upstream::ClusterInfo> upstreamCluster() const PURE;
};

/**
 * Callback of request/response frame.
 */
class EncodingCallbacks {
public:
  virtual ~EncodingCallbacks() = default;

  /**
   * If encoding success then this method will be called to write the data to upstream
   * or downstream connection and notify the generic proxy if the encoding is completed.
   * @param buffer encoding result buffer.
   * @param end_stream if last frame is encoded.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer, bool end_stream) PURE;

  /**
   * If encoding failure then this method will be called.
   * @param reason the reason of encoding failure.
   */
  virtual void onEncodingFailure(absl::string_view reason = {}) PURE;

  /**
   * The route that the request is matched to. This is optional when encoding the response
   * (by server codec) because the request may not be matched to any route  and the
   * response is created by the server codec directly. This must be valid when encoding the
   * request (by client codec).
   * @return the route that the request is matched to.
   */
  virtual OptRef<const RouteEntry> routeEntry() const PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
