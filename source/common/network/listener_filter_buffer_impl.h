#pragma once

#include <bits/stdint-uintn.h>

#include <atomic>
#include <functional>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/listener_filter_buffer.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Network {

using ListenerFilterBufferOnCloseCb = std::function<void()>;
using ListenerFilterBufferOnDataCb = std::function<void()>;

enum class PeekState {
  // Peek data status successful.
  Done,
  // Need to try again.
  Again,
  // Error to peek data.
  Error
};

class ListenerFilterBufferImpl : public ListenerFilterBuffer, Logger::Loggable<Logger::Id::filter> {
public:
  ListenerFilterBufferImpl(IoHandle& io_handle, Event::Dispatcher& dispatcher,
                           ListenerFilterBufferOnCloseCb close_cb,
                           ListenerFilterBufferOnDataCb on_data_cb, uint64_t buffer_size)
      : io_handle_(io_handle), dispatcher_(dispatcher), on_close_cb_(close_cb),
        on_data_cb_(on_data_cb), buffer_(new Buffer::OwnedImpl()) {
    // Since we need the single slice for peek from the socket, so initialize that
    // single slice.
    auto reservation = buffer_->reserveSingleSlice(buffer_size);
    reservation.commit(buffer_size);
  }

  const Buffer::ConstRawSlice rawSlice() const override;

  uint64_t copyOut(Buffer::Instance& buffer, uint64_t length) override;
  uint64_t drain(uint64_t length) override;
  uint64_t length() const override { return buffer_->length(); }

  /**
   * trigger the data peek from the socket.
   */
  PeekState peekFromSocket();

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
  void onFileEvent(uint32_t events);

  IoHandle& io_handle_;
  Event::Dispatcher& dispatcher_;
  ListenerFilterBufferOnCloseCb on_close_cb_;
  ListenerFilterBufferOnDataCb on_data_cb_;

  // The buffer for the data peeked from the socket.
  Buffer::InstancePtr buffer_;
  // the size of valid data.
  uint64_t data_size_{0};
  // This is used for counting the drained data size before
  // drain the data from actual socket.
  uint64_t drained_size_{0};
};

using ListenerFilterBufferImplPtr = std::unique_ptr<ListenerFilterBufferImpl>;

} // namespace Network
} // namespace Envoy