#include "test/common/stats/real_thread_test_base.h"

namespace Envoy {
namespace Stats {

ThreadLocalStoreNoMocksMixin::ThreadLocalStoreNoMocksMixin()
    : alloc_(symbol_table_), store_(std::make_unique<ThreadLocalStoreImpl>(alloc_)),
      scope_(*store_->rootScope()), pool_(symbol_table_) {}

ThreadLocalStoreNoMocksMixin::~ThreadLocalStoreNoMocksMixin() {
  if (store_ != nullptr) {
    store_->shutdownThreading();
  }
}

StatName ThreadLocalStoreNoMocksMixin::makeStatName(absl::string_view name) {
  return pool_.add(name);
}

ThreadLocalRealThreadsMixin::ThreadLocalRealThreadsMixin(uint32_t num_threads)
    : RealThreadsTestHelper(num_threads) {
  runOnMainBlocking([this]() { store_->initializeThreading(*main_dispatcher_, *tls_); });
}

ThreadLocalRealThreadsMixin::~ThreadLocalRealThreadsMixin() {
  // TODO(chaoqin-li1123): clean this up when we figure out how to free the threading resources in
  // RealThreadsTestHelper.
  shutdownThreading();
  exitThreads([this]() { store_.reset(); });
}

void ThreadLocalRealThreadsMixin::shutdownThreading() {
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
