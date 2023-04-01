
#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/drain_decision.h"

#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Extended options from request or response to control the behavior of the
 * generic proxy filter.
 * All these options are optional for the simple ping-pong use case.
 */
class ExtendedOptions {
public:
  /**
   * @return the stream id of the request or response. This is used to match the
   * downstream request with the upstream response.

   * NOTE: In most cases, the stream id is not needed and will be ignored completely.
   * The stream id is only used when we can't match the downstream request
   * with the upstream response by the active stream instance self directly.
   * For example, when the multiple downstream requests are multiplexed into one
   * upstream connection.
   */
  uint64_t streamId() const { return stream_id_; }
  /**
   * Set the stream id of the request or response.
   * @param stream_id the stream id of the request or response.
   */
  void setStreamId(uint64_t stream_id) {
    stream_id_ = stream_id;
    has_stream_id_ = true;
  }
  /**
   * @return whether the stream id is set.
   */
  bool hasStreamId() const { return has_stream_id_; }

  /**
   * @return whether the current request requires an upstream response.
   */
  bool waitResponse() const { return wait_response_; }

  /**
   * Set whether the current request requires an upstream response.
   * @param wait_response whether the current request requires an upstream response.
   */
  void setWaitResponse(bool wait_response) { wait_response_ = wait_response; }

  /**
   * @return whether the downstream/upstream connection should be drained after
   * current active requests are finished.
   */
  bool drainClose() const { return drain_close_; }
  /**
   * Set whether the downstream/upstream connection should be drained after
   * current active requests are finished.
   * @param drain_close whether the downstream/upstream connection should be drained
   * after current active requests are finished.
   */
  void setDrainClose(bool drain_close) { drain_close_ = drain_close; }

  /**
   * @return whether the current request/response is a heartbeat request/response.
   */
  bool isHeartbeat() const { return is_heartbeat_; }
  /**
   * Set whether the current request/response is a heartbeat request/response.
   * @param is_heartbeat whether the current request/response is a heartbeat
   * request/response.
   */
  void setHeartbeat(bool is_heartbeat) { is_heartbeat_ = is_heartbeat; }

private:
  uint64_t stream_id_{};
  bool has_stream_id_{};

  bool wait_response_{true};
  bool drain_close_{};
  bool is_heartbeat_{};
};

/**
 * Decoder callback of request.
 */
class RequestDecoderCallback {
public:
  virtual ~RequestDecoderCallback() = default;

  /**
   * If request decoding success then this method will be called.
   * @param request request from decoding.
   * @param options extended options from request.
   */
  virtual void onDecodingSuccess(RequestPtr request, ExtendedOptions options) PURE;

  /**
   * If request decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Decoder callback of Response.
 */
class ResponseDecoderCallback {
public:
  virtual ~ResponseDecoderCallback() = default;

  /**
   * If response decoding success then this method will be called.
   * @param response response from decoding.
   * @param options extended options from response.
   */
  virtual void onDecodingSuccess(ResponsePtr response, ExtendedOptions options) PURE;

  /**
   * If response decoding failure then this method will be called.
   */
  virtual void onDecodingFailure() PURE;
};

/**
 * Encoder callback of request.
 */
class RequestEncoderCallback {
public:
  virtual ~RequestEncoderCallback() = default;

  /**
   * If request encoding success then this method will be called.
   * @param buffer encoding result buffer.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer) PURE;
};

/**
 * Encoder callback of Response.
 */
class ResponseEncoderCallback {
public:
  virtual ~ResponseEncoderCallback() = default;

  /**
   * If response encoding success then this method will be called.
   * @param buffer encoding result buffer.
   */
  virtual void onEncodingSuccess(Buffer::Instance& buffer) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
