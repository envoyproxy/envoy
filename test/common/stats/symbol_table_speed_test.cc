// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/stats/symbol_table_impl.h"

#include "test/test_common/utility.h"

#include "absl/synchronization/blocking_counter.h"
#include "testing/base/public/benchmark.h"

namespace {

// This class functions like absl::Notification except the usage of SignalAll()
// appears to trigger tighter simultaneous wakeups in multiple threads. Note
// that the synchronization mechanism in
//     https://github.com/abseil/abseil-cpp/blob/master/absl/synchronization/notification.h
// has timing properties that seem to resultl in fewer collisions.
class Notifier {
public:
  Notifier() : cond_(false) {}

  void notify() {
    absl::MutexLock lock(&mutex_);
    cond_ = true;
    cond_var_.SignalAll();
  }

  void wait() {
    absl::MutexLock lock(&mutex_);
    while (!cond_) {
      cond_var_.Wait(&mutex_);
    }
  }

private:
  absl::Mutex mutex_;
  bool cond_ GUARDED_BY(mutex_);
  absl::CondVar cond_var_;
};

} // namespace

namespace Envoy {
namespace Stats {

void symbolTableTest(benchmark::State& state) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  // Make 100 threads, each of which will race to encode an overlapping set of
  // symbols, triggering corner-cases in SymbolTable::toSymbol.
  constexpr int num_threads = 100;
  std::vector<Thread::ThreadPtr> threads;
  threads.reserve(num_threads);
  Notifier creation, access, wait;
  absl::BlockingCounter creates(num_threads), accesses(num_threads);
  SymbolTable table;
  for (int i = 0; i < num_threads; ++i) {
    threads.push_back(
        thread_factory.createThread([i, &creation, &access, &creates, &accesses, &state, &table]() {
          // Rotate between 20 different symbols to try to get some
          // contention. Based on a logging print statement in
          // contention. Based on a logging print statement in
          // SymbolTable::toSymbol(), this appears to trigger creation-races,
          // even when compiled with optimization.
          std::string stat_name_string = absl::StrCat("symbol", i % 20);

          // Block each thread on waking up a common condition variable,
          // so we make it likely to race on creation.
          creation.wait();
          StatNameStorage initial(stat_name_string, table);
          creates.DecrementCount();
          access.wait();

          for (auto _ : state) {
            for (int j = 0; j < 100; ++j) {
              StatNameStorage second(stat_name_string, table);
              second.free(table);
            }
          }
          initial.free(table);
          accesses.DecrementCount();
        }));
  }
  creation.notify();
  creates.Wait();

  // But when we access the already-existing symbols, we guarantee that no
  // further mutex contentions occur.
  access.notify();
  accesses.Wait();

  for (auto& thread : threads) {
    thread->join();
  }
}

} // namespace Stats
} // namespace Envoy

static void BM_CreateRace(benchmark::State& state) { Envoy::Stats::symbolTableTest(state); }
BENCHMARK(BM_CreateRace);

int main(int argc, char** argv) {
  Envoy::Thread::MutexBasicLockable lock;
  Envoy::Logger::Context logger_context(spdlog::level::warn,
                                        Envoy::Logger::Logger::DEFAULT_LOG_FORMAT, lock);
  benchmark::Initialize(&argc, argv);

  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  benchmark::RunSpecifiedBenchmarks();
}
