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

// Invoked when an asynchronous write() completes.
using WriteCallback = absl::AnyInvocable<void(ExternalBufferStatus)>;

// Invoked when an asynchronous read() completes. On Ok, `data` holds exactly
// the requested bytes and ownership is transferred to the callee. On Error,
// `data` is nullptr.
using ReadCallback = absl::AnyInvocable<void(ExternalBufferStatus, Buffer::InstancePtr)>;

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
// Because an implementation may perform real I/O, data operations report
// completion through a callback. Callbacks are always invoked on the owning
// dispatcher's thread, but an implementation MAY invoke a callback either
// synchronously (re-entrantly, before the operation returns) or asynchronously
// on a later event-loop iteration -- whichever fits its backing store. A purely
// in-memory store completes reads synchronously; a disk/remote store typically
// posts and completes later. Callers must tolerate both. Destroying the buffer
// cancels any pending asynchronous callbacks; they will not fire afterwards.
class ExternalBuffer {
public:
  virtual ~ExternalBuffer() = default;

  // Writes an owned `data` buffer to the end of the store (the store is
  // append-only), taking ownership of it. `cb` is invoked once the bytes are
  // durable.
  //
  // The caller issues at most one write at a time: it must not call write()
  // again until the previous write's completion callback has fired. This
  // single-writer contract means an implementation never has to maintain its own
  // write queue or reason about overlapping/out-of-order writes -- the
  // BufferManager serializes writes and queues the backlog. An implementation
  // that cannot keep up applies back-pressure simply by deferring `cb`; the
  // caller will not issue the next write until it fires.
  //
  // The caller transfers an already-owned buffer rather than a borrowed
  // reference, so an implementation never has to grab the bytes synchronously to
  // keep them alive across asynchronous I/O -- that ownership hand-off is
  // storage-agnostic and is done once by the BufferManager.
  virtual void write(Buffer::InstancePtr data, WriteCallback cb) PURE;

  // Reads `length` bytes starting at absolute byte offset `offset`. It is an
  // error to request a range that extends past the current length(). `cb`
  // receives the requested bytes and may be invoked synchronously (see the class
  // comment).
  virtual void read(uint64_t offset, uint64_t length, ReadCallback cb) PURE;

  // Total number of bytes written (and acknowledged) so far. Reflects only
  // writes whose completion callback has already fired.
  virtual uint64_t length() const PURE;
};

using ExternalBufferPtr = std::unique_ptr<ExternalBuffer>;

// Creates ExternalBuffer instances. A filter creates one buffer per stream. The
// factory is shared across streams and must be thread-safe-by-construction
// (stateless) or worker-local.
class ExternalBufferFactory {
public:
  virtual ~ExternalBufferFactory() = default;

  // Creates a fresh, empty buffer that uses `dispatcher` to deliver completion
  // callbacks.
  virtual ExternalBufferPtr createBuffer(Event::Dispatcher& dispatcher) PURE;
};

using ExternalBufferFactoryPtr = std::unique_ptr<ExternalBufferFactory>;
using ExternalBufferFactorySharedPtr = std::shared_ptr<ExternalBufferFactory>;

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
