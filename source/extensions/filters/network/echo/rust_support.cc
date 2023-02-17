#include "contrib/rust_echo/filters/network/source/rust_support.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

bool data_available(const WaitForDataHandle* handle) { return handle->ready_; }

bool is_end_stream(const WaitForDataHandle* handle) { return handle->end_stream_; }

Envoy::Buffer::Instance* data_as_slice(const WaitForDataHandle* handle) { return handle->buffer_; }

void register_future_with_executor(const Executor* executor, rust::Box<FutureHandle> future) {
  executor->pending_poll_handles_.push_back(PollHandle{std::move(future)});
}

const WaitForDataHandle* notify_waiting_for_data(const Executor* executor) {
  if (!executor->next_data_handle_) {
    executor->next_data_handle_ = std::make_unique<WaitForDataHandle>();
  }

  return &*executor->next_data_handle_;
}

void wake_executor(const Executor*) {}

void drop_executor(const Executor*) {}

Envoy::Network::Connection* connection(Envoy::Network::ReadFilterCallbacks* cbs) {
  return &cbs->connection();
}

void write_to(Envoy::Network::Connection* connection, Envoy::Buffer::Instance* buffer,
              bool end_stream) {
  connection->write(*buffer, end_stream);
}
} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
