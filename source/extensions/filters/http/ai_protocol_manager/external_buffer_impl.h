#pragma once

#include <memory>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// A pure in-memory ExternalBuffer. The bytes are held in an owned buffer on the
// heap; there is no real backing store.
//
// Reads complete synchronously (the bytes are already in memory): the callback
// runs on-stack from within read(). append() completions are posted to the
// dispatcher, modelling a write that is not durable until acknowledged and
// flushing out ordering/lifetime bugs in callers. Because writes are durable the
// instant they land in memory, there is never any in-flight backlog: the high
// watermark is never crossed and flow control is effectively a no-op. This
// implementation therefore does NOT bound the resident footprint -- it exists as
// a reference and for tests. A disk- or remote-backed implementation is what
// makes the footprint guarantee real, and it reuses the exact same interface.
class InMemoryExternalBuffer : public ExternalBuffer {
public:
  explicit InMemoryExternalBuffer(Event::Dispatcher& dispatcher);
  ~InMemoryExternalBuffer() override;

  // ExternalBuffer
  void append(Buffer::InstancePtr data, AppendCallback cb) override;
  void read(uint64_t offset, uint64_t length, ReadCallback cb) override;
  uint64_t length() const override { return data_.length(); }
  void setWatermarks(uint32_t high_watermark, uint32_t low_watermark,
                     ExternalBufferWatermarkCallbacks& callbacks) override;

private:
  Event::Dispatcher& dispatcher_;
  Buffer::OwnedImpl data_;

  // Watermark configuration. Retained for interface completeness; the in-memory
  // store never accumulates in-flight bytes so these are never acted upon.
  ExternalBufferWatermarkCallbacks* watermark_callbacks_{};
  uint32_t high_watermark_{0};
  uint32_t low_watermark_{0};

  // Set to false in the destructor and shared (by value) with every posted
  // completion callback so a callback scheduled before destruction becomes a
  // no-op instead of touching a freed buffer.
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

// Factory for InMemoryExternalBuffer. Stateless and safe to share across
// streams and workers.
class InMemoryExternalBufferFactory : public ExternalBufferFactory {
public:
  ExternalBufferPtr createBuffer(Event::Dispatcher& dispatcher) override {
    return std::make_unique<InMemoryExternalBuffer>(dispatcher);
  }
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
