#include "real_threads_test_helper.h"

#include "absl/synchronization/barrier.h"
#include "utility.h"

namespace Envoy {
namespace Thread {

RealThreadsTestHelper::RealThreadsTestHelper(uint32_t num_threads)
    : api_(Api::createApiForTest()), num_threads_(num_threads),
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

std::function<void()> RealThreadsTestHelper::BlockingBarrier::run(std::function<void()> f) {
  return [this, f]() {
    f();
    decrementCount();
  };
}

std::function<void()> RealThreadsTestHelper::BlockingBarrier::decrementCountFn() {
  return [this] { decrementCount(); };
}

void RealThreadsTestHelper::shutdownThreading() {
  runOnMainBlocking([this]() {
    if (!tls_->isShutdown()) {
      tls_->shutdownGlobalThreading();
    }
    tls_->shutdownThread();
  });
}

void RealThreadsTestHelper::exitThreads(std::function<void()> cleanup) {
  for (Event::DispatcherPtr& dispatcher : thread_dispatchers_) {
    dispatcher->post([&dispatcher]() { dispatcher->exit(); });
  }

  for (ThreadPtr& thread : threads_) {
    thread->join();
  }

  main_dispatcher_->post([this, cleanup]() {
    if (cleanup != nullptr) {
      cleanup();
    }
    tls_.reset();
    main_dispatcher_->exit();
  });
  main_thread_->join();
}

void RealThreadsTestHelper::runOnAllWorkersBlocking(std::function<void()> work) {
  runOnAllWorkers(work)();
}

std::function<void()> RealThreadsTestHelper::runOnAllWorkers(std::function<void()> work) {
  auto start_barrier = std::make_shared<absl::Barrier>(num_threads_);
  auto blocking_barrier = std::make_shared<BlockingBarrier>(num_threads_);
  for (Event::DispatcherPtr& thread_dispatcher : thread_dispatchers_) {
    thread_dispatcher->post(blocking_barrier->run([work, start_barrier]() {
      start_barrier->Block();
      work();
    }));
  }

  // When run, this closure will block on the destruction of the blocking barrier.
  auto waiter = [blocking_barrier]() {};
  blocking_barrier.reset();
  return waiter;
}

void RealThreadsTestHelper::runOnMainBlocking(std::function<void()> work) { runOnMain(work)(); }

std::function<void()> RealThreadsTestHelper::runOnMain(std::function<void()> work) {
  auto blocking_barrier = std::make_shared<BlockingBarrier>(1);
  main_dispatcher_->post(blocking_barrier->run([work]() { work(); }));
  auto waiter = [blocking_barrier]() {};
  blocking_barrier.reset();
  return waiter;
}

void RealThreadsTestHelper::mainDispatchBlock() {
  // To ensure all stats are freed we have to wait for a few posts() to clear.
  // First, wait for the main-dispatcher to initiate the cross-thread TLS cleanup.
  runOnMainBlocking([]() {});
}

void RealThreadsTestHelper::tlsBlock() {
  runOnAllWorkersBlocking([]() {});
}

void RealThreadsTestHelper::workerThreadFn(uint32_t thread_index,
                                           BlockingBarrier& blocking_barrier) {
  thread_dispatchers_[thread_index] =
      api_->allocateDispatcher(absl::StrCat("test_worker_", thread_index));
  blocking_barrier.decrementCount();
  thread_dispatchers_[thread_index]->run(Event::Dispatcher::RunType::RunUntilExit);
}

void RealThreadsTestHelper::mainThreadFn(BlockingBarrier& blocking_barrier) {
  main_dispatcher_ = api_->allocateDispatcher("test_main_thread");
  blocking_barrier.decrementCount();
  main_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
}

} // namespace Thread
} // namespace Envoy
