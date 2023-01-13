#pragma once

#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/real_threads_test_helper.h"

namespace Envoy {
namespace Stats {

// See full context https://github.com/envoyproxy/envoy/pull/23921
// This is a helper class used in thread local store test and the future
// lazy init stats speed test, which works as the base for ThreadLocalStore related tests.
class ThreadLocalStoreNoMocksMixin {
public:
  ThreadLocalStoreNoMocksMixin();
  ~ThreadLocalStoreNoMocksMixin();

  StatName makeStatName(absl::string_view name);

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  ThreadLocalStoreImplPtr store_;
  Scope& scope_;
  StatNamePool pool_;
};

// Helper base class for thread local stats testing in multiple-workers setup.
class ThreadLocalRealThreadsMixin : public Thread::RealThreadsTestHelper,
                                    public ThreadLocalStoreNoMocksMixin {
protected:
  static constexpr uint32_t NumScopes = 1000;
  static constexpr uint32_t NumIters = 35;

public:
  ThreadLocalRealThreadsMixin(uint32_t num_threads);

  ~ThreadLocalRealThreadsMixin();

  void shutdownThreading();
};

} // namespace Stats
} // namespace Envoy
