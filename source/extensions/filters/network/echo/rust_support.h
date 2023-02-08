#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"

#include "rust/cxx.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Echo {

struct FutureHandle;

void poll_future(FutureHandle& handle) noexcept;

struct PollHandle {
  rust::Box<FutureHandle> future_;
};

struct WaitForDataHandle {
  bool ready_;
  bool end_stream_;
  Envoy::Buffer::Instance* buffer_;
};

bool data_available(const WaitForDataHandle* handle);

bool is_end_stream(const WaitForDataHandle* handle);

Envoy::Buffer::Instance* data_as_slice(const WaitForDataHandle* handle);

// Per thread future execution context.
class Executor {
public:
  explicit Executor(Envoy::Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  void onData(Envoy::Buffer::Instance& buffer, bool end_stream) {
    if (next_data_handle_) {
      const auto handle = std::move(next_data_handle_);

      handle->ready_ = true;
      handle->buffer_ = &buffer;
      handle->end_stream_ = end_stream;

      poll();
    }
  }

  void poll() {
    for (auto& future : pending_poll_handles_) {
      poll_future(*future.future_);
    }
  }

  Envoy::Event::Dispatcher& dispatcher_;
  mutable std::vector<PollHandle> pending_poll_handles_;

  mutable std::unique_ptr<WaitForDataHandle> next_data_handle_;
};

void register_future_with_executor(const Executor* executor, rust::Box<FutureHandle> future);

const WaitForDataHandle* notify_waiting_for_data(const Executor* executor);

void wake_executor(const Executor*);

void drop_executor(const Executor*);

Envoy::Network::Connection* connection(Envoy::Network::ReadFilterCallbacks* cbs);
void write_to(Envoy::Network::Connection* connection, Envoy::Buffer::Instance* buffer,
              bool end_stream);
} // namespace Echo
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
