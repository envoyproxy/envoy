#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/tracing/trace_context.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * Flags of stream frame. This is used to control the behavior of the generic proxy filter.
 * All these flags could be ignored for the simple ping-pong use case.
 */
class FrameFlags {
public:
  static constexpr uint32_t FLAG_EMPTY = 0x0000;
  static constexpr uint32_t FLAG_END_STREAM = 0x0001;
  static constexpr uint32_t FLAG_ONE_WAY = 0x0002;
  static constexpr uint32_t FLAG_DRAIN_CLOSE = 0x0004;
  static constexpr uint32_t FLAG_HEARTBEAT = 0x0008;

  /**
   * Construct FrameFlags with stream flags and end stream flag. The stream flags MUST be
   * same for all frames of the same stream.
   * @param stream_id the stream id of the request or response.
   * @param flags flags of the current frame. Only the flags that defined in FrameFlags
   * could be used. Multiple flags could be combined by bitwise OR.
   * @param frame_tags frame tags of the current frame. The meaning of the frame tags is
   * application protocol specific.
   */
  FrameFlags(uint64_t stream_id = 0, uint32_t flags = FLAG_END_STREAM, uint32_t frame_tags = 0)
      : stream_id_(stream_id), flags_(flags), frame_tags_(frame_tags) {}

  /**
   * @return the stream id of the request or response. All frames of the same stream
   * MUST have the same stream id.
   */
  uint64_t streamId() const { return stream_id_; }

  /**
   * @return frame tags of the current frame. The meaning of the frame tags is application
   * protocol specific. This allows the creator of the frame to attach additional information to the
   * frame and get it by the receiver of the frame without parsing the frame payload or dynamic
   * cast.
   * For example, the frame tags could be used to indicate the type of the frame by the server
   * codec. Then the client codec could get the frame type without dynamic cast.
   */
  uint32_t frameTags() const { return frame_tags_; }

  /**
   * @return the raw flags of the current stream frame.
   */
  uint32_t rawFlags() const { return flags_; }

  /**
   * @return whether the current frame is the last frame of the request or response.
   */
  bool endStream() const { return flags_ & FLAG_END_STREAM; }

  /**
   * @return whether the downstream/upstream connection should be drained after
   * current active stream are finished.
   * NOTE: Only the response header frame's drainClose() flag will be used.
   */
  bool drainClose() const { return flags_ & FLAG_DRAIN_CLOSE; }

  /**
   * @return whether the stream is one way stream. If request is one way stream, the
   * generic proxy filter will not wait for the response from the upstream.
   * NOTE: Only the request header frame's oneWayStream() flag will be used.
   */
  bool oneWayStream() const { return flags_ & FLAG_ONE_WAY; }

  /**
   * @return whether the current request/response is a heartbeat request/response.
   * NOTE: Only the header frame's heartbeat() flag will be used. In most cases, the heartbeat
   * should be handled directly by the underlying codec and should not be exposed to the generic
   * proxy filter. This should only be used when we need to send the heartbeat to the peer.
   */
  bool heartbeat() const { return flags_ & FLAG_HEARTBEAT; }

private:
  uint64_t stream_id_{};
  uint32_t flags_{};
  uint32_t frame_tags_{};
};

/**
 * Stream frame interface. This is used to represent the stream frame of request or response.
 */
class StreamFrame {
public:
  virtual ~StreamFrame() = default;

  /**
   * Get stream frame flags of current frame. The default implementation returns empty flags
   * that could be used for the simple ping-pong use case.
   * @return FrameFlags of the current frame.
   */
  virtual FrameFlags frameFlags() const { return {}; }
};

/**
 * Common frame that used to represent any data or structure of L7 protocols. No specific
 * interface is provided for the common frame.
 */
class CommonFrame : public StreamFrame {};
using CommonFramePtr = std::unique_ptr<CommonFrame>;

/**
 * Header frame of generic request or response. This provide some basic interfaces that are
 * used to get/set attributes of the request or response.
 * NOTE: Header frame should always be the first frame of the request or response. And there
 * has no requirement that the header frame could only contain the 'header' of L7 protocols.
 * For example, for short HTTP request, the header frame could contain the whole request
 * header map, body, and even trailer.
 */
class HeaderFrame : public StreamFrame {
public:
  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Get application protocol of generic stream.
   *
   * @return A string view representing the application protocol of the generic stream behind
   * the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Iterate over all generic stream metadata entries.
   *
   * @param callback supplies the iteration callback.
   */
  virtual void forEach(IterateCallback /*callback*/) const {};

  /**
   * Get generic stream metadata value by key.
   *
   * @param key The metadata key of string view type.
   * @return The optional metadata value of string_view type.
   */
  virtual absl::optional<absl::string_view> get(absl::string_view /*key*/) const { return {}; }

  /**
   * Set new generic stream metadata key/value pair.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void set(absl::string_view /*key*/, absl::string_view /*val*/) {}

  /**
   * Erase generic stream metadata by key.
   * @param key The metadata key of string view type.
   */
  virtual void erase(absl::string_view /*key*/) {}
};

// Alias for backward compatibility.
using StreamBase = HeaderFrame;

/**
 * Interface of generic request. This is derived from StreamFrame that contains the request
 * specific information. First frame of the request MUST be a RequestHeaderFrame.
 */
class RequestHeaderFrame : public HeaderFrame {
public:
  /**
   * Get request host.
   *
   * @return The host of generic request. The meaning of the return value may be different For
   * different application protocols. It typically should be domain, VIP, or service name that
   * used to represents target service instances.
   */
  virtual absl::string_view host() const { return {}; }

  /**
   * Get request path.
   *
   * @return The path of generic request. The meaning of the return value may be different For
   * different application protocols. It typically should be RPC service name that used to
   * represents set of method or functionality provided by target service.
   */
  virtual absl::string_view path() const { return {}; }

  /**
   * Get request method.
   *
   * @return The method of generic request. The meaning of the return value may be different For
   * different application protocols.
   */
  virtual absl::string_view method() const { return {}; }
};

using RequestHeaderFramePtr = std::unique_ptr<RequestHeaderFrame>;
using RequestCommonFrame = CommonFrame;
using RequestCommonFramePtr = std::unique_ptr<RequestCommonFrame>;

// Alias for backward compatibility.
using StreamRequest = RequestHeaderFrame;
using StreamRequestPtr = RequestHeaderFramePtr;
using Request = RequestHeaderFrame;
using RequestPtr = std::unique_ptr<Request>;

enum class Event {
  Timeout,
  ConnectionTimeout,
  ConnectionClosed,
  LocalConnectionClosed,
  ConnectionFailure,
};

/**
 * The Status type is used by the generic proxy to indicate statuses or error types
 * to the application protocol codec. This is application protocol independent.
 */
using Status = absl::Status;
using StatusCode = absl::StatusCode;

/**
 * Generic stream status. The Status is used by the application protocol codec to
 * indicate the status of the response. The meaning of status code is application
 * protocol specific.
 */
struct StreamStatus {
public:
  StreamStatus() = default;
  StreamStatus(int code, bool ok) : code_(code), ok_(ok) {}

  // Returns true if the status indicates success. This will be used for tracing, logging
  // or stats purposes.
  ABSL_MUST_USE_RESULT bool ok() const { return ok_; }

  // Returns the status code value. The code will be used for tracing, logging or stats
  // purposes. The specific code value is application protocol specific.
  ABSL_MUST_USE_RESULT int code() const { return code_; }

private:
  int code_{};
  bool ok_{true};
};

/**
 * Interface of generic response. This is derived from StreamFrame that contains the response
 * specific information. First frame of the response MUST be a ResponseHeaderFrame.
 */
class ResponseHeaderFrame : public HeaderFrame {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual StreamStatus status() const { return {}; }
};

using ResponseHeaderFramePtr = std::unique_ptr<ResponseHeaderFrame>;
using ResponseCommonFrame = CommonFrame;
using ResponseCommonFramePtr = std::unique_ptr<ResponseCommonFrame>;

// Alias for backward compatibility.
using StreamResponse = ResponseHeaderFrame;
using StreamResponsePtr = ResponseHeaderFramePtr;
using Response = ResponseHeaderFrame;
using ResponsePtr = std::unique_ptr<Response>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
