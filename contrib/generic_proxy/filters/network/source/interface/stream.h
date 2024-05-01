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
 * Stream flags from request or response to control the behavior of the
 * generic proxy filter. This is mainly used as part of FrameFlags.
 * All these flags could be ignored for the simple ping-pong use case.
 */
class StreamFlags {
public:
  StreamFlags(uint64_t stream_id = 0, bool one_way_stream = false, bool drain_close = false,
              bool is_heartbeat = false)
      : stream_id_(stream_id), one_way_stream_(one_way_stream), drain_close_(drain_close),
        is_heartbeat_(is_heartbeat) {}

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
   * @return whether the stream is one way stream. If request is one way stream, the
   * generic proxy filter will not wait for the response from the upstream.
   */
  bool oneWayStream() const { return one_way_stream_; }

  /**
   * @return whether the downstream/upstream connection should be drained after
   * current active stream are finished.
   */
  bool drainClose() const { return drain_close_; }

  /**
   * @return whether the current request/response is a heartbeat request/response.
   * NOTE: It would be better to handle heartbeat request/response by another L4
   * filter. Then the generic proxy filter can be used for the simple ping-pong
   * use case.
   */
  bool isHeartbeat() const { return is_heartbeat_; }

private:
  uint64_t stream_id_{0};

  bool one_way_stream_{false};
  bool drain_close_{false};
  bool is_heartbeat_{false};
};

/**
 * Flags of stream frame. This is used to control the behavior of the generic proxy filter.
 * All these flags could be ignored for the simple ping-pong use case.
 */
class FrameFlags {
public:
  /**
   * Construct FrameFlags with stream flags and end stream flag. The stream flags MUST be
   * same for all frames of the same stream.
   * @param stream_flags StreamFlags of the stream.
   * @param end_stream whether the current frame is the last frame of the request or response.
   * @param frame_tags frame tags of the current frame. The meaning of the frame tags is
   * application protocol specific.
   */
  FrameFlags(StreamFlags stream_flags = StreamFlags(), bool end_stream = true,
             uint32_t frame_tags = 0)
      : stream_flags_(stream_flags), end_stream_(end_stream), frame_tags_(frame_tags) {}

  /**
   * Get flags of stream that the frame belongs to. The flags MUST be same for all frames of the
   * same stream. Copy semantics is used because the flags are lightweight (only 16 bytes for now).
   * @return StreamFlags of the stream.
   */
  StreamFlags streamFlags() const { return stream_flags_; }

  /**
   * @return whether the current frame is the last frame of the request or response.
   */
  bool endStream() const { return end_stream_; }

  /**
   * @return frame tags of the current frame. The meaning of the frame tags is application
   * protocol specific. This allows the creator of the frame to attach additional information to the
   * frame and get it by the receiver of the frame without parsing the frame payload or dynamic
   * cast.
   * For example, the frame tags could be used to indicate the type of the frame by the server
   * codec. Then the client codec could get the frame type without dynamic cast.
   */
  uint32_t frameTags() const { return frame_tags_; }

private:
  StreamFlags stream_flags_{};

  // Default to true for backward compatibility.
  bool end_stream_{true};
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

using StreamFramePtr = std::unique_ptr<StreamFrame>;
using StreamFrameSharedPtr = std::shared_ptr<StreamFrame>;

class StreamBase : public StreamFrame {
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

  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }
};

/**
 * Interface of generic request. This is derived from StreamFrame that contains the request
 * specific information. First frame of the request MUST be a StreamRequest.
 *
 * NOTE: using interface that provided by the TraceContext as the interface of generic request here
 * to simplify the tracing integration. This is not a good design. This should be changed in the
 * future.
 */
class StreamRequest : public StreamBase {
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

using StreamRequestPtr = std::unique_ptr<StreamRequest>;
using StreamRequestSharedPtr = std::shared_ptr<StreamRequest>;
// Alias for backward compatibility.
using Request = StreamRequest;
using RequestPtr = std::unique_ptr<Request>;
using RequestSharedPtr = std::shared_ptr<Request>;

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
 * specific information. First frame of the response MUST be a StreamResponse.
 */
class StreamResponse : public StreamBase {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual StreamStatus status() const { return {}; }
};

using StreamResponsePtr = std::unique_ptr<StreamResponse>;
using StreamResponseSharedPtr = std::shared_ptr<StreamResponse>;
// Alias for backward compatibility.
using Response = StreamResponse;
using ResponsePtr = std::unique_ptr<Response>;
using ResponseSharedPtr = std::shared_ptr<Response>;

template <class T> class StreamFramePtrHelper {
public:
  StreamFramePtrHelper(StreamFramePtr frame) : frame_(std::move(frame)) {
    if (auto typed_frame_ptr = dynamic_cast<T*>(frame_.get()); typed_frame_ptr != nullptr) {
      // If the frame is the expected type, wrap it in the typed frame unique pointer.
      frame_.release();
      typed_frame_ = std::unique_ptr<T>{typed_frame_ptr};
    }
  }

  StreamFramePtr frame_;
  std::unique_ptr<T> typed_frame_;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
