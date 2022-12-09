#include "test/common/stats/real_thread_test_base.h"

namespace Envoy {
namespace Stats {

ThreadLocalStoreNoMocksTestBase::ThreadLocalStoreNoMocksTestBase()
    : alloc_(symbol_table_), store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)),
      pool_(symbol_table_) {}

ThreadLocalStoreNoMocksTestBase::~ThreadLocalStoreNoMocksTestBase() {
  if (store_ != nullptr) {
    store_->shutdownThreading();
  }
}

StatName ThreadLocalStoreNoMocksTestBase::makeStatName(absl::string_view name) {
  return pool_.add(name);
}

ThreadLocalRealThreadsTestBase::ThreadLocalRealThreadsTestBase(uint32_t num_threads)
    : RealThreadsTestHelper(num_threads) {
  runOnMainBlocking([this]() { store_->initializeThreading(*main_dispatcher_, *tls_); });
}

ThreadLocalRealThreadsTestBase::~ThreadLocalRealThreadsTestBase() {
  // TODO(chaoqin-li1123): clean this up when we figure out how to free the threading resources in
  // RealThreadsTestHelper.
  shutdownThreading();
  exitThreads([this]() { store_.reset(); });
}

void ThreadLocalRealThreadsTestBase::shutdownThreading() {
  runOnMainBlocking([this]() {
    if (!tls_->isShutdown()) {
      tls_->shutdownGlobalThreading();
    }
    store_->shutdownThreading();
    tls_->shutdownThread();
  });
}

} // namespace Stats
} // namespace Envoy
