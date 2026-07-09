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
// runs on-stack from within read(). write() completions are posted to the
// dispatcher, modelling a write that is not durable until acknowledged and
// flushing out ordering/lifetime bugs in callers. This implementation does NOT
// bound the resident footprint -- it exists as a reference and for tests. A
// disk- or remote-backed implementation is what makes the footprint guarantee
// real, and it reuses the exact same interface.
class InMemoryExternalBuffer : public ExternalBuffer {
public:
  explicit InMemoryExternalBuffer(Event::Dispatcher& dispatcher);
  ~InMemoryExternalBuffer() override;

  // ExternalBuffer
  void write(Buffer::InstancePtr data, WriteCallback cb) override;
  void read(uint64_t offset, uint64_t length, ReadCallback cb) override;
  uint64_t length() const override { return data_.length(); }

private:
  Event::Dispatcher& dispatcher_;
  Buffer::OwnedImpl data_;

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
