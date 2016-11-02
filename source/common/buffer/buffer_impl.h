#pragma once

#include "envoy/buffer/buffer.h"

#include "common/event/libevent.h"

// Forward decls to avoid leaking libevent headers to rest of program.
struct evbuffer_cb_info;
typedef void (*evbuffer_cb_func)(evbuffer* buffer, const evbuffer_cb_info* info, void* arg);

namespace Buffer {

/**
 * Wraps an allocated and owned evbuffer.
 */
class OwnedImpl : public Instance {
public:
  OwnedImpl();
  OwnedImpl(const std::string& data);
  OwnedImpl(const Instance& data);
  OwnedImpl(const void* data, uint64_t size);

  // Instance
  void add(const void* data, uint64_t size) override;
  void add(const std::string& data) override;
  void add(const Instance& data) override;
  void commit(RawSlice* iovecs, uint64_t num_iovecs) override;
  void drain(uint64_t size) override;
  uint64_t getRawSlices(RawSlice* out, uint64_t out_size) const override;
  uint64_t length() const override;
  void* linearize(uint32_t size) override;
  void move(Instance& rhs) override;
  void move(Instance& rhs, uint64_t length) override;
  int read(int fd, uint64_t max_length) override;
  uint64_t reserve(uint64_t length, RawSlice* iovecs, uint64_t num_iovecs) override;
  ssize_t search(const void* data, uint64_t size, size_t start) const override;
  void setCallback(Callback callback) override;
  int write(int fd) override;

private:
  void onBufferChange(const evbuffer_cb_info& info);

  static const evbuffer_cb_func buffer_cb_; // Static callback used for all evbuffer callbacks.
                                            // This allows us to add/remove by value.

  Event::Libevent::BufferPtr buffer_;
  Callback cb_; // The per buffer callback. Invoked via the buffer_cb_ static thunk.
};

} // Buffer
