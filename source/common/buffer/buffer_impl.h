#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"

#include "common/event/libevent.h"

namespace Envoy {
namespace Buffer {

class LibEventInstance : public Instance {
public:
  // Allows access into the underlying buffer for move() optimizations.
  virtual Event::Libevent::BufferPtr& buffer() PURE;
  // Called after accessing the memory in buffer() directly to allow any post-processing.
  virtual void postProcess() PURE;
};

/**
 * Wraps an allocated and owned evbuffer.
 *
 * Note that due to the internals of move() accessing buffer(), OwnedImpl is not
 * compatible with non-LibEventInstance buffers.
 */
class OwnedImpl : public LibEventInstance {
public:
  OwnedImpl();
  OwnedImpl(const std::string& data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);

  // LibEventInstance
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void copyOut(size_t start, uint64_t size, void* data) const override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  int read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;
  int write(int fd) override;
  void postProcess() override {}

  Event::Libevent::BufferPtr& buffer() override { return buffer_; }

private:
  Event::Libevent::BufferPtr buffer_;
};

} // namespace Buffer
} // namespace Envoy
