#pragma once

#include "envoy/buffer/buffer.h"

#include "common/event/libevent.h"

namespace Buffer {

/**
 * Base implementation for libevent backed buffers.
 */
class ImplBase : public Instance {
public:
  // Instance
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void drain(uint64_t size) override;
  std::vector<RawSlice> getRawSlices() const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;

private:
  /**
   * @return evbuffer& the backing evbuffer.
   */
  virtual evbuffer& buffer() const PURE;
};

/**
 * Wraps a non-owned evbuffer.
 */
class WrappedImpl : public ImplBase {
public:
  WrappedImpl(evbuffer* buffer) : buffer_(buffer) {}

private:
  // ImplBase
  evbuffer& buffer() const override { return *buffer_; }

  evbuffer* buffer_;
};

/**
 * Wraps an allocated and owned evbuffer.
 */
class OwnedImpl : public ImplBase {
public:
  OwnedImpl();
  OwnedImpl(const std::string& data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);

private:
  // ImplBase
  evbuffer& buffer() const override { return *buffer_; }

  Event::Libevent::BufferPtr buffer_;
};

} // Buffer
