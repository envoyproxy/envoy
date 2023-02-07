#include "envoy/buffer/buffer.h"
#include "envoy/network/filter.h"
#include "rust/cxx.h"
#include "envoy/network/connection.h"

struct FutureHandle;

struct PollHandle {
  rust::Box<FutureHandle> future_;
};

// Per thread future execution context.
class Executor {
public:
  explicit Executor(Envoy::Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

  Envoy::Event::Dispatcher& dispatcher_;
  std::vector<PollHandle> pending_poll_handles_;
};

void register_future_with_executor(Executor& executor, rust::Box<FutureHandle> future) {
  executor.pending_poll_handles_.push_back(PollHandle{std::move(future)});
}
