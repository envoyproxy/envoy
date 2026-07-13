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

// An append-only, random-access byte store with asynchronous I/O. The
// BufferManager offloads a payload into it (potentially off-heap or off-host)
// and later streams ranges back out.
//
// I/O reports completion through a callback on the owning dispatcher's thread. An
// implementation MAY invoke that callback synchronously (reentrantly, before the
// call returns) or asynchronously on a later iteration -- whichever fits its
// store -- and the BufferManager tolerates both. Destroying the buffer cancels
// any pending callbacks; they will not fire afterwards.
class ExternalBuffer {
public:
  virtual ~ExternalBuffer() = default;

  // Appends owned `data` to the store, taking ownership; `cb` fires once the bytes
  // are durable. The caller issues at most one write at a time -- it will not call
  // write() again until `cb` fires -- so an implementation needs no write queue of
  // its own and can apply back-pressure simply by deferring `cb`.
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
