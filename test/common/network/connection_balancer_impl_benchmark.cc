#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "source/common/common/assert.h"
#include "source/common/network/connection_balancer_impl.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Network {
namespace {

// A minimal balanced connection handler that only tracks a connection count, which is all the
// connection balancers touch on the accept path.
class BenchmarkConnectionHandler : public BalancedConnectionHandler {
public:
  uint64_t numConnections() const override {
    return num_connections_.load(std::memory_order_relaxed);
  }
  void preIncNumConnections() override { num_connections_.fetch_add(1, std::memory_order_relaxed); }
  void postIncNumConnections() override {}
  void post(ConnectionSocketPtr&&) override { PANIC("not implemented"); }
  void onAcceptWorker(ConnectionSocketPtr&&, bool, bool,
                      const std::optional<std::string>&) override {
    PANIC("not implemented");
  }

private:
  std::atomic<uint64_t> num_connections_{0};
};

// Measures only the per-accept balancing cost, the exact balancer's mutex and handler scan versus
// the lock-free increment used when the kernel steers connections. It does not measure CPU cache or
// `NUMA` locality, which depend on the kernel and `NIC` and are out of scope for a micro-benchmark.
template <typename BalancerType> void benchmarkPickTargetHandler(::benchmark::State& state) {
  // Shared by every benchmark thread. Thread zero builds it before the start barrier that opens the
  // timed loop, so the other threads observe it safely once inside the loop.
  static std::unique_ptr<ConnectionBalancer> balancer;
  static std::vector<std::unique_ptr<BenchmarkConnectionHandler>> handlers;
  if (state.thread_index() == 0) {
    balancer = std::make_unique<BalancerType>();
    handlers.clear();
    handlers.reserve(state.threads());
    for (int i = 0; i < state.threads(); i++) {
      handlers.push_back(std::make_unique<BenchmarkConnectionHandler>());
      balancer->registerHandler(*handlers.back());
    }
  }

  BalancedConnectionHandler* current_handler = nullptr;
  for (auto _ : state) { // NOLINT: Silences warning about dead store
    if (current_handler == nullptr) {
      current_handler = handlers[state.thread_index()].get();
    }
    ::benchmark::DoNotOptimize(&balancer->pickTargetHandler(*current_handler));
  }
  state.SetItemsProcessed(state.iterations());

  if (state.thread_index() == 0) {
    handlers.clear();
    balancer.reset();
  }
}

BENCHMARK_TEMPLATE(benchmarkPickTargetHandler, ExactConnectionBalancerImpl)
    ->ThreadRange(1, 32)
    ->UseRealTime();
BENCHMARK_TEMPLATE(benchmarkPickTargetHandler, NopConnectionBalancerImpl)
    ->ThreadRange(1, 32)
    ->UseRealTime();

} // namespace
} // namespace Network
} // namespace Envoy
