#include "source/common/event/dispatcher_impl.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "absl/synchronization/blocking_counter.h"

namespace Envoy {
namespace Thread {

class RealThreadsTestHelper {
protected:
  // Helper class to block on a number of multi-threaded operations occurring.
  class BlockingBarrier {
  public:
    explicit BlockingBarrier(uint32_t count) : blocking_counter_(count) {}
    ~BlockingBarrier() { blocking_counter_.Wait(); }

    /**
     * Returns a function that first executes 'f', and then decrements the count
     * toward unblocking the scope. This is intended to be used as a post() callback.
     *
     * @param f the function to run prior to decrementing the count.
     */
    std::function<void()> run(std::function<void()> f);

    /**
     * @return a function that, when run, decrements the count, intended for passing to post().
     */
    std::function<void()> decrementCountFn();

    void decrementCount() { blocking_counter_.DecrementCount(); }

  private:
    absl::BlockingCounter blocking_counter_;
  };

  explicit RealThreadsTestHelper(uint32_t num_threads);
  // TODO(chaoqin-li1123): Clean up threading resources from the destructor when we figure out how
  // to handle different destruction orders of thread local object.
  ~RealThreadsTestHelper() = default;
  // Shutdown thread local instance.
  void shutdownThreading();
  // Post exit signal and wait for main thread and worker threads to join.
  void exitThreads();
  // Run the callback in all the workers, block until the callback has finished in all threads.
  void runOnAllWorkersBlocking(std::function<void()> work);
  // Run the callback in main thread, block until the callback has been executed in main thread.
  void runOnMainBlocking(std::function<void()> work);
  // Post an empty callback to main thread and block until all the previous callbacks have been
  // executed.
  void mainDispatchBlock();
  // Post an empty callback to worker threads and block until all the previous callbacks have been
  // executed.
  void tlsBlock();

  ThreadLocal::Instance& tls() { return *tls_; }

  Api::Api& api() { return *api_; }

  // TODO(chaoqin-li1123): make these variables private when we figure out how to clean up the
  // threading resources inside the helper class.
  Api::ApiPtr api_;
  Event::DispatcherPtr main_dispatcher_;
  std::vector<Event::DispatcherPtr> thread_dispatchers_;
  ThreadLocal::InstanceImplPtr tls_;
  ThreadPtr main_thread_;
  std::vector<ThreadPtr> threads_;

private:
  void workerThreadFn(uint32_t thread_index, BlockingBarrier& blocking_barrier);

  void mainThreadFn(BlockingBarrier& blocking_barrier);

  const uint32_t num_threads_;
  ThreadFactory& thread_factory_;
};

} // namespace Thread
} // namespace Envoy
