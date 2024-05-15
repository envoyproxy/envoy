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
 * The start time of the request or response that provided by the codec to the generic
 * proxy filter. The generic proxy filter could use this to calculate the latency of
 * the request or response.
 */
struct StartTime {
  SystemTime start_time{};
  MonotonicTime start_time_monotonic{};
};

/**
 * Callbacks of ServerCodec.
 */
class ServerCodecCallbacks {
public:
  virtual ~ServerCodecCallbacks() = default;

  /**
   * If request decoding success then this method will be called. This method should
   * be called first for the whole request.
   *
   * @param header_frame request header frame from decoding.
   * @param start_time optional start time of the request. If not provided, the generic
   * proxy filter will use the current time as the start time of the request. This makes
   * sense when the codec takes a long time to decode the request and the start time is
   * not the time that calling the method.
   *
   * NOTE: This method will be called only once for the whole request.
   */
  virtual void onDecodingSuccess(RequestHeaderFramePtr header_frame,
                                 absl::optional<StartTime> start_time = {}) PURE;

  /**
   * If request decoding success and additional frames are received for the request then
   * this method will be called. This method should be called after RequestHeaderFrame
   * is handled.
   *
   * @param common_frame request common frame from decoding.
   *
   * NOTE: This method will be called multiple times for the multiple frames request.
   * stream_id could be used to correlate frames of same request and end_stream could be
   * used tell generic proxy filter if the request is completed.
   */
  virtual void onDecodingSuccess(RequestCommonFramePtr common_frame) PURE;

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
   *
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
   * If response decoding success then this method will be called. This method should
   * be called first for the whole response.
   *
   * @param header_frame response frame from decoding.
   * @param start_time optional start time of the response. If not provided, the generic
   * proxy filter will use the current time as the start time of the response. This makes
   * sense when the codec takes a long time to decode the response and the start time is
   * not the time that calling the method.
   *
   * NOTE: This method will be called only once for the whole response.
   */
  virtual void onDecodingSuccess(ResponseHeaderFramePtr header_frame,
                                 absl::optional<StartTime> start_time = {}) PURE;

  /**
   * If response decoding success and additional frames are received for the response then
   * this method will be called. This method should be called after ResponseHeaderFrame
   * is handled.
   *
   * @param common_frame response frame from decoding.
   *
   * NOTE: This method will be called multiple times for the multiple frames request.
   * stream_id could be used to correlate frames of same request and end_stream could be
   * used tell generic proxy filter if the request is completed.
   */
  virtual void onDecodingSuccess(ResponseCommonFramePtr common_frame) PURE;

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
