#include "real_threads_test.h"

#include "absl/synchronization/barrier.h"
#include "utility.h"

namespace Envoy {
namespace Thread {

RealThreadsTestBase::RealThreadsTestBase(uint32_t num_threads)
    : num_threads_(num_threads), api_(Api::createApiForTest()),
      thread_factory_(api_->threadFactory()) {
  // This is the same order as InstanceImpl::initialize in source/server/server.cc.
  thread_dispatchers_.resize(num_threads_);
  {
    BlockingBarrier blocking_barrier(num_threads_ + 1);
    main_thread_ = thread_factory_.createThread(
        [this, &blocking_barrier]() { mainThreadFn(blocking_barrier); });
    for (uint32_t i = 0; i < num_threads_; ++i) {
      threads_.emplace_back(thread_factory_.createThread(
          [this, i, &blocking_barrier]() { workerThreadFn(i, blocking_barrier); }));
    }
  }
  runOnMainBlocking([this]() {
    tls_ = std::make_unique<ThreadLocal::InstanceImpl>();
    tls_->registerThread(*main_dispatcher_, true);
    for (Event::DispatcherPtr& dispatcher : thread_dispatchers_) {
      // Worker threads must be registered from the main thread, per assert in registerThread().
      tls_->registerThread(*dispatcher, false);
    }
  });
}

void RealThreadsTestBase::shutdownThreading() {
  runOnMainBlocking([this]() {
    if (!tls_->isShutdown()) {
      tls_->shutdownGlobalThreading();
    }
    tls_->shutdownThread();
  });
}

void RealThreadsTestBase::exitThreads() {
  for (Event::DispatcherPtr& dispatcher : thread_dispatchers_) {
    dispatcher->post([&dispatcher]() { dispatcher->exit(); });
  }

  for (ThreadPtr& thread : threads_) {
    thread->join();
  }

  main_dispatcher_->post([this]() {
    tls_.reset();
    main_dispatcher_->exit();
  });
  main_thread_->join();
}

void RealThreadsTestBase::runOnAllWorkersBlocking(std::function<void()> work) {
  absl::Barrier start_barrier(num_threads_);
  BlockingBarrier blocking_barrier(num_threads_);
  for (Event::DispatcherPtr& thread_dispatcher : thread_dispatchers_) {
    thread_dispatcher->post(blocking_barrier.run([work, &start_barrier]() {
      start_barrier.Block();
      work();
    }));
  }
}

void RealThreadsTestBase::runOnMainBlocking(std::function<void()> work) {
  BlockingBarrier blocking_barrier(1);
  main_dispatcher_->post(blocking_barrier.run([work]() { work(); }));
}

void RealThreadsTestBase::mainDispatchBlock() {
  // To ensure all stats are freed we have to wait for a few posts() to clear.
  // First, wait for the main-dispatcher to initiate the cross-thread TLS cleanup.
  runOnMainBlocking([]() {});
}

void RealThreadsTestBase::tlsBlock() {
  runOnAllWorkersBlocking([]() {});
}

void RealThreadsTestBase::workerThreadFn(uint32_t thread_index, BlockingBarrier& blocking_barrier) {
  thread_dispatchers_[thread_index] =
      api_->allocateDispatcher(absl::StrCat("test_worker_", thread_index));
  blocking_barrier.decrementCount();
  thread_dispatchers_[thread_index]->run(Event::Dispatcher::RunType::RunUntilExit);
}

void RealThreadsTestBase::mainThreadFn(BlockingBarrier& blocking_barrier) {
  main_dispatcher_ = api_->allocateDispatcher("test_main_thread");
  blocking_barrier.decrementCount();
  main_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Thread
} // namespace Envoy
