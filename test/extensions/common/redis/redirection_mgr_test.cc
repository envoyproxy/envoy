#include <atomic>
#include <chrono>

#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "exe/platform_impl.h"

#include "extensions/common/redis/redirection_mgr_impl.h"

#include "test/extensions/filters/network/common/redis/mocks.h"
#include "test/extensions/filters/network/redis_proxy/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Redis {

class RedirectionMgrTest : public testing::Test {
public:
  RedirectionMgrTest()
      : cluster_name_("fake_cluster"),
        redirection_manager_(
            std::make_shared<RedirectionManagerImpl>(dispatcher_, cm_, time_system_)) {
    time_system_.setMonotonicTime(std::chrono::seconds(1));
    map_.emplace("fake_cluster", mock_cluster_);
    ON_CALL(cm_, clusters()).WillByDefault(Return(map_));
  }
  ~RedirectionMgrTest() override = default;

  // Advance simulation time by increment milliseconds, waiting on nthreads other threads at each
  // point, before continuing. This must be called only by a single thread.
  void advanceTime(MonotonicTime&& end_time, uint32_t nthreads = 0,
                   std::chrono::milliseconds&& increment = std::chrono::milliseconds(1000)) {
    if (nthreads == 0) {
      // This is a special case. Ignore increment and set the time to end_time.
      time_system_.setMonotonicTime(end_time);
    } else {
      MonotonicTime current_time = time_system_.monotonicTime();
      while (current_time < end_time) {
        {
          Thread::LockGuard lg(time_mutex_);
          // Wait for all waiting threads to arrive. Wait on a separate condition variable that is
          // signaled by each waiting thread.
          while (nthreads_waiting_ < nthreads) {
            setter_wait_cv_.wait(time_mutex_);
          }
          current_time += increment;
          if (current_time > end_time) {
            // Ensure that end_time is not overshot.
            current_time = end_time;
          }
          time_system_.setMonotonicTime(current_time);
          wait_cv_.notifyAll();
        }
        // Wait for the waiting threads to all reach this "exit" gate. This ensures that all threads
        // properly enter and exit the time-advancing loop without getting ahead or behind.
        while (nthreads_going_ < nthreads) {
          std::this_thread::yield();
        }
        // Release the gate for waiting threads.
        nthreads_going_ = 0;
      }
    }
  }

  // Wait until simulation time reaches end_time.
  void waitForTime(MonotonicTime&& end_time) {
    while (time_system_.monotonicTime() < end_time) {
      {
        Thread::LockGuard lg(time_mutex_);
        // Only notify the time-advancing thread that we're about to wait with time_mutex_ locked.
        // This ensures that this thread is properly waiting before the time-advancing thread gets
        // to notify this thread that time has been advanced. Otherwise, this thread might miss
        // the notification.
        nthreads_waiting_++;
        setter_wait_cv_.notifyOne();
        wait_cv_.wait(time_mutex_);
        nthreads_waiting_--;
      }
      nthreads_going_++;
      // Wait at this gate until the time setting threads releases it.
      while (nthreads_going_ > 0) {
        std::this_thread::yield();
      }
    }
  }

  RedirectionManagerImpl::ClusterInfoSharedPtr clusterInfo(const std::string& cluster_name) {
    Thread::LockGuard lock(redirection_manager_->map_mutex_);
    return redirection_manager_->info_map_[cluster_name];
  }

  const std::string cluster_name_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Upstream::ClusterManager::ClusterInfoMap map_;
  Upstream::MockClusterMockPrioritySet mock_cluster_;
  Event::SimulatedTimeSystem time_system_;
  std::shared_ptr<RedirectionManagerImpl> redirection_manager_;
  RedirectionManager::HandlePtr handle_;
  std::atomic<uint32_t> callback_count_{};
  std::atomic<uint32_t> nthreads_waiting_{};
  std::atomic<uint32_t> nthreads_going_{};
  Thread::CondVar wait_cv_;
  Thread::CondVar setter_wait_cv_;
  Thread::MutexBasicLockable time_mutex_;
  PlatformImpl platform_;
};

// This test exercises the redirection manager's basic functionality with redirect events being
// registered via 2 threads. The manager is notified of events on valid registered clusters and
// invalid unregistered cluster names.
TEST_F(RedirectionMgrTest, Basic) {
  handle_ = redirection_manager_->registerCluster(cluster_name_, std::chrono::milliseconds(1000), 1,
                                                  [&]() { callback_count_++; });
  RedirectionManagerImpl::ClusterInfoSharedPtr cluster_info = clusterInfo(cluster_name_);

  Thread::ThreadPtr thread_1 = platform_.threadFactory().createThread([&]() {
    waitForTime(MonotonicTime(std::chrono::seconds(1)));
    EXPECT_TRUE(redirection_manager_->onRedirection(cluster_name_));
    waitForTime(MonotonicTime(std::chrono::seconds(2)));
    redirection_manager_->onRedirection(cluster_name_);
    waitForTime(MonotonicTime(std::chrono::seconds(3)));
  });
  Thread::ThreadPtr thread_2 = platform_.threadFactory().createThread([&]() {
    waitForTime(MonotonicTime(std::chrono::seconds(2)));
    redirection_manager_->onRedirection(cluster_name_);
    waitForTime(MonotonicTime(std::chrono::seconds(3)));
  });

  advanceTime(MonotonicTime(std::chrono::seconds(3)), 2);
  thread_1->join();
  thread_2->join();

  EXPECT_GE(callback_count_, 2);
  EXPECT_EQ(cluster_info->redirects_count_, 0);
  EXPECT_EQ(cluster_info->last_callback_time_ms_.load(), 2000);
  EXPECT_EQ(cluster_info->min_time_between_triggering_, std::chrono::milliseconds(1000));
  EXPECT_EQ(cluster_info->redirects_threshold_, 1);

  callback_count_ = 0;
  advanceTime(MonotonicTime(std::chrono::seconds(5)));
  EXPECT_FALSE(redirection_manager_->onRedirection("unregistered_cluster_name"));
  EXPECT_EQ(callback_count_, 0);

  handle_.reset();
  EXPECT_FALSE(redirection_manager_->onRedirection(cluster_name_));
}

// This test records a high number of events for a cluster using 2 threads. Simulated time
// is advanced without thread synchronization for up to 2 seconds during the threads' activity
// to simulate possible thread timing issues.
TEST_F(RedirectionMgrTest, HighVolume) {
  handle_ = redirection_manager_->registerCluster(cluster_name_, std::chrono::seconds(2), 1000,
                                                  [&]() { callback_count_++; });
  RedirectionManagerImpl::ClusterInfoSharedPtr cluster_info = clusterInfo(cluster_name_);
  uint32_t thread1_callback_count = 0;
  uint32_t thread2_callback_count = 0;

  Thread::ThreadPtr thread_1 = platform_.threadFactory().createThread([&]() {
    for (uint32_t i = 1; i < 61; i += 2) {
      waitForTime(MonotonicTime(std::chrono::seconds(i)));
      for (uint32_t j = 0; j < 2000; j++) {
        if (redirection_manager_->onRedirection(cluster_name_)) {
          thread1_callback_count++;
        }
      }
    }
  });
  Thread::ThreadPtr thread_2 = platform_.threadFactory().createThread([&]() {
    for (uint32_t i = 1; i < 61; i += 2) {
      waitForTime(MonotonicTime(std::chrono::seconds(i)));
      for (uint32_t j = 0; j < 2000; j++) {
        if (redirection_manager_->onRedirection(cluster_name_)) {
          thread2_callback_count++;
        }
      }
    }
  });

  // Synchronize all threads every 2 seconds of simulated time.
  for (uint32_t i = 1; i < 61; i += 2) {
    advanceTime(MonotonicTime(std::chrono::seconds(i)), 2, std::chrono::seconds(1));
  }
  thread_1->join();
  thread_2->join();

  EXPECT_EQ(callback_count_, thread1_callback_count + thread2_callback_count);
  EXPECT_EQ(callback_count_, 30);
}

} // namespace Redis
} // namespace Common
} // namespace Extensions
} // namespace Envoy
