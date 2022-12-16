#pragma once

#include "source/common/stats/thread_local_store.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/real_threads_test_helper.h"

namespace Envoy {
namespace Stats {

class ThreadLocalStoreNoMocksTestBase {
public:
  ThreadLocalStoreNoMocksTestBase();
  ~ThreadLocalStoreNoMocksTestBase();

  StatName makeStatName(absl::string_view name);

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  ThreadLocalStoreImplPtr store_;
  Scope& scope_;
  StatNamePool pool_;
};

// Helper base class for threadlocal stats testing in multiple-workers setup.
// This is used in threadlocal store tests.
class ThreadLocalRealThreadsTestBase : public Thread::RealThreadsTestHelper,
                                       public ThreadLocalStoreNoMocksTestBase {
protected:
  static constexpr uint32_t NumScopes = 1000;
  static constexpr uint32_t NumIters = 35;

public:
  ThreadLocalRealThreadsTestBase(uint32_t num_threads);

  ~ThreadLocalRealThreadsTestBase();

  void shutdownThreading();
};

} // namespace Stats
} // namespace Envoy
