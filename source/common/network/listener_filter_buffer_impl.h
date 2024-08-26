#pragma once

#include <functional>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/network/io_handle.h"
#include "envoy/network/listener_filter_buffer.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Network {

class ListenerFilterBufferImpl;
using ListenerFilterBufferOnCloseCb = std::function<void(bool)>;
using ListenerFilterBufferOnDataCb = std::function<void(ListenerFilterBufferImpl&)>;

enum class PeekState {
  // Peek data status successful.
  Done,
  // Need to try again.
  Again,
  // Error to peek data.
  Error,
  // Connection closed by remote.
  RemoteClose,
};

class ListenerFilterBufferImpl : public ListenerFilterBuffer, Logger::Loggable<Logger::Id::filter> {
public:
  ListenerFilterBufferImpl(IoHandle& io_handle, Event::Dispatcher& dispatcher,
                           ListenerFilterBufferOnCloseCb close_cb,
                           ListenerFilterBufferOnDataCb on_data_cb, bool on_data_cb_disabled,
                           uint64_t buffer_size);

  // ListenerFilterBuffer
  const Buffer::ConstRawSlice rawSlice() const override;
  bool drain(uint64_t length) override;

  /**
   * Trigger the data peek from the socket.
   */
  PeekState peekFromSocket();

  void reset() { io_handle_.resetFileEvents(); }

  void activateFileEvent(uint32_t events);
  uint64_t capacity() const { return buffer_size_; }
  void resetCapacity(uint64_t size);
  void disableOnDataCallback(bool on_data_cb_disabled) {
    on_data_cb_disabled_ = on_data_cb_disabled;
  }

private:
  absl::Status onFileEvent(uint32_t events);

  IoHandle& io_handle_;
  Event::Dispatcher& dispatcher_;
  ListenerFilterBufferOnCloseCb on_close_cb_;
  ListenerFilterBufferOnDataCb on_data_cb_;

  bool on_data_cb_disabled_{};
  // The size of buffer;
  uint64_t buffer_size_;
  // The size of valid data.
  uint64_t data_size_{0};

  // The buffer for the data peeked from the socket.
  std::unique_ptr<uint8_t[]> buffer_;
  // The start of buffer.
  uint8_t* base_;
};

using ListenerFilterBufferImplPtr = std::unique_ptr<ListenerFilterBufferImpl>;

} // namespace Network
} // namespace Envoy
