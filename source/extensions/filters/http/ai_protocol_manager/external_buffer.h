#pragma once

#include <cstdint>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"

#include "absl/functional/any_invocable.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Status of a completed external-buffer I/O operation.
enum class ExternalBufferStatus {
  // The operation completed successfully.
  Ok,
  // The operation failed (e.g. a backing-store I/O error). After an error the
  // buffer should be considered unusable and the caller is expected to fail
  // the stream.
  Error,
};

// Invoked when an asynchronous append() completes.
using AppendCallback = absl::AnyInvocable<void(ExternalBufferStatus)>;

// Invoked when an asynchronous read() completes. On Ok, `data` holds exactly
// the requested bytes and ownership is transferred to the callee. On Error,
// `data` is nullptr.
using ReadCallback = absl::AnyInvocable<void(ExternalBufferStatus, Buffer::InstancePtr)>;

// Callbacks an ExternalBuffer uses to signal flow-control state to its
// producer. These mirror Envoy's watermark-buffer convention: onAboveHigh
// fires when the volume of in-flight (not-yet-durable) bytes crosses the high
// watermark, onBelowLow when it drains back below the low watermark.
//
// The producer is expected to stop appending after onAboveHighWatermark() and
// resume after onBelowLowWatermark(). Honoring this is what keeps the resident
// memory footprint bounded when the backing store cannot keep up with ingest.
class ExternalBufferWatermarkCallbacks {
public:
  virtual ~ExternalBufferWatermarkCallbacks() = default;

  // The amount of in-flight (not-yet-persisted) data has exceeded the high
  // watermark. The producer should stop appending until onBelowLowWatermark().
  virtual void onAboveHighWatermark() PURE;

  // In-flight data has drained below the low watermark; the producer may
  // resume appending.
  virtual void onBelowLowWatermark() PURE;
};

// An append-only, random-access byte store with asynchronous I/O.
//
// The AI Protocol Manager streams a request body into an ExternalBuffer so it
// can be held off the hot path (and potentially off-heap or off-host) while
// the full payload is parsed and validated. Routing and admission decisions
// require the fully parsed payload, so the body must be retained; offloading it
// here keeps it out of the connection manager's in-memory buffers. Once parsing
// decides what to do, the bytes are streamed back into the filter chain via
// read().
//
// Because an implementation may perform real I/O, every data operation is
// asynchronous and reports completion through a callback. All callbacks are
// invoked on the owning dispatcher's thread and never re-entrantly from within
// the call that scheduled them. Destroying the buffer cancels any pending
// callbacks; they will not fire afterwards.
class ExternalBuffer {
public:
  virtual ~ExternalBuffer() = default;

  // Appends `data` to the end of the buffer. `data` is fully drained (the bytes
  // are transferred to the buffer). `cb` is invoked once the bytes are durable.
  // Appends are ordered: completion callbacks fire in the order append() was
  // called.
  virtual void append(Buffer::Instance& data, AppendCallback cb) PURE;

  // Reads `length` bytes starting at absolute byte offset `offset`. It is an
  // error to request a range that extends past the current length(). `cb`
  // receives the requested bytes.
  virtual void read(uint64_t offset, uint64_t length, ReadCallback cb) PURE;

  // Total number of bytes appended (and acknowledged) so far. Reflects only
  // appends whose completion callback has already fired.
  virtual uint64_t length() const PURE;

  // Registers flow-control callbacks and the high/low watermarks (in bytes) on
  // in-flight append data. A high watermark of 0 disables flow control.
  // `callbacks` must outlive the buffer.
  virtual void setWatermarks(uint32_t high_watermark, uint32_t low_watermark,
                             ExternalBufferWatermarkCallbacks& callbacks) PURE;
};

using ExternalBufferPtr = std::unique_ptr<ExternalBuffer>;

// Creates ExternalBuffer instances. A filter creates one buffer per stream. The
// factory is shared across streams and must be thread-safe-by-construction
// (stateless) or worker-local.
class ExternalBufferFactory {
public:
  virtual ~ExternalBufferFactory() = default;

  // Creates a fresh, empty buffer that uses `dispatcher` to deliver completion
  // and watermark callbacks.
  virtual ExternalBufferPtr createBuffer(Event::Dispatcher& dispatcher) PURE;
};

using ExternalBufferFactoryPtr = std::unique_ptr<ExternalBufferFactory>;
using ExternalBufferFactorySharedPtr = std::shared_ptr<ExternalBufferFactory>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
