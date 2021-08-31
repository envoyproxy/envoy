#pragma once

#include <bits/stdint-uintn.h>

#include <atomic>
#include <functional>
#include <memory>

#include "envoy/network/io_handle.h"
#include "envoy/network/listener_filter_buffer.h"

namespace Envoy {
namespace Network {

using ListenerFilterBufferOnCloseCb = std::function<void()>;
using ListenerFilterBufferOnDataCb = std::function<void()>;

class ListenerFilterBufferImpl : public ListenerFilterBuffer {
public:
  ListenerFilterBufferImpl(IoHandle& io_handle, Event::Dispatcher& dispatcher,
                           ListenerFilterBufferOnCloseCb close_cb,
                           ListenerFilterBufferOnDataCb on_data_cb, uint64_t buffer_size)
      : io_handle_(io_handle), dispatcher_(dispatcher), on_close_cb_(close_cb),
        on_data_cb_(on_data_cb), buffer_(new uint8_t[buffer_size]), buffer_size_(buffer_size),
        base_(buffer_.get()) {}

  uint64_t copyOut(void* buffer, uint64_t length) override;
  uint64_t drain(uint64_t length) override;
  uint64_t length() const override { return data_size_; }

  /**
   * sync the drain to the actual socket.
   */
  bool drainFromSocket();

  void initialize() {
    io_handle_.initializeFileEvent(
        dispatcher_, [this](uint32_t events) { onFileEvent(events); },
        Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Closed);
  }

  void reset() { io_handle_.resetFileEvents(); }

private:
  uint64_t drainedSize() const { return base_ - buffer_.get(); }
  void onFileEvent(uint32_t events);

  IoHandle& io_handle_;
  Event::Dispatcher& dispatcher_;
  ListenerFilterBufferOnCloseCb on_close_cb_;
  ListenerFilterBufferOnDataCb on_data_cb_;

  // The buffer for the data peeked from the socket.
  std::unique_ptr<uint8_t[]> buffer_;
  // The max size of the buffer.
  uint64_t buffer_size_;
  // The pointer of the beginning of the valid data.
  uint8_t* base_{nullptr};
  // The size of valid data.
  uint64_t data_size_{0};
  // true means already drain the data from the actual socket.
  bool drained_to_socket{false};
};

using ListenerFilterBufferImplPtr = std::unique_ptr<ListenerFilterBufferImpl>;

} // namespace Network
} // namespace Envoy