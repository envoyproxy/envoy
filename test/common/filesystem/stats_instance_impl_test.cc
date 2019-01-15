#include <chrono>
#include <string>

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/stats_instance_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::Throw;

namespace Envoy {

class StatsInstanceImplTest : public testing::Test {
protected:
  StatsInstanceImplTest()
      : stats_instance_(std::chrono::milliseconds(10000), Thread::threadFactoryForTest(),
                        stats_store_, file_system_) {}

  const std::chrono::milliseconds timeout_40ms_{40};
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Filesystem::MockInstance> file_system_;
  Filesystem::StatsInstanceImpl stats_instance_;
};

TEST_F(StatsInstanceImplTest, flushToLogFilePeriodically) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system_, createFile(_))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));

  Filesystem::StatsFileSharedPtr stats_file =
      stats_instance_.createStatsFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  stats_file->write("test");

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 1) {
      file->write_event_.wait(file->write_mutex_);
    }
  }

  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test2", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  // make sure timer is re-enabled on callback call
  stats_file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 2) {
      file->write_event_.wait(file->write_mutex_);
    }
  }
}

TEST_F(StatsInstanceImplTest, flushToLogFileOnDemand) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system_, createFile(_))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));

  Filesystem::StatsFileSharedPtr stats_file =
      stats_instance_.createStatsFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));

  // The first write to a given file will start the flush thread, which can flush
  // immediately (race on whether it will or not). So do a write and flush to
  // get that state out of the way, then test that small writes don't trigger a flush.
  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void*, size_t num_bytes) -> Api::SysCallSizeResult {
        return {static_cast<ssize_t>(num_bytes), 0};
      }));
  stats_file->write("prime-it");
  stats_file->flush();
  uint32_t expected_writes = 1;
  {
    Thread::LockGuard lock(file->write_mutex_);
    EXPECT_EQ(expected_writes, file->num_writes_);
  }

  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  stats_file->write("test");

  {
    Thread::LockGuard lock(file->write_mutex_);
    EXPECT_EQ(expected_writes, file->num_writes_);
  }

  stats_file->flush();
  expected_writes++;
  {
    Thread::LockGuard lock(file->write_mutex_);
    EXPECT_EQ(expected_writes, file->num_writes_);
  }

  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test2", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  // make sure timer is re-enabled on callback call
  stats_file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();
  expected_writes++;

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != expected_writes) {
      file->write_event_.wait(file->write_mutex_);
    }
  }
}

TEST_F(StatsInstanceImplTest, reopenFile) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;

  Sequence sq;
  EXPECT_CALL(file_system_, createFile(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));
  Filesystem::StatsFileSharedPtr stats_file =
      stats_instance_.createStatsFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*file, write_(_, _))
      .InSequence(sq)
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("before", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  stats_file->write("before");
  timer->callback_();

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 1) {
      file->write_event_.wait(file->write_mutex_);
    }
  }

  EXPECT_CALL(*file, close_()).InSequence(sq);
  EXPECT_CALL(*file, open_()).InSequence(sq);

  EXPECT_CALL(*file, write_(_, _))
      .InSequence(sq)
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("reopened", written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  EXPECT_CALL(*file, close_()).InSequence(sq);

  stats_file->reopen();
  stats_file->write("reopened");
  timer->callback_();

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 2) {
      file->write_event_.wait(file->write_mutex_);
    }
  }
}

TEST_F(StatsInstanceImplTest, reopenThrows) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system_, createFile(_))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));

  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        UNREFERENCED_PARAMETER(buffer);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  //  Sequence sq;
  //  EXPECT_CALL(*file, open_()).InSequence(sq);

  Filesystem::StatsFileSharedPtr stats_file =
      stats_instance_.createStatsFile("", dispatcher, mutex, timeout_40ms_);

  stats_file->write("test write");
  timer->callback_();
  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 1) {
      file->write_event_.wait(file->write_mutex_);
    }
  }

  Sequence sq;
  EXPECT_CALL(*file, close_()).InSequence(sq);
  EXPECT_CALL(*file, open_()).InSequence(sq).WillOnce(Throw(EnvoyException("open failed")));

  stats_file->reopen();

  stats_file->write("this is to force reopen");
  timer->callback_();

  {
    Thread::LockGuard lock(file->open_mutex_);
    while (file->num_open_ != 1) {
      file->open_event_.wait(file->open_mutex_);
    }
  }

  // write call should not cause any exceptions
  stats_file->write("random data");
  timer->callback_();
}

TEST_F(StatsInstanceImplTest, bigDataChunkShouldBeFlushedWithoutTimer) {
  NiceMock<Event::MockDispatcher> dispatcher;
  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system_, createFile(_))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));

  Filesystem::StatsFileSharedPtr stats_file =
      stats_instance_.createStatsFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        std::string expected("a");
        EXPECT_EQ(expected, written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  stats_file->write("a");

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 1) {
      file->write_event_.wait(file->write_mutex_);
    }
  }

  // First write happens without waiting on thread_flush_. Now make a big string and it should be
  // flushed even when timer is not enabled
  EXPECT_CALL(*file, write_(_, _))
      .WillOnce(Invoke([](const void* buffer, size_t num_bytes) -> Api::SysCallSizeResult {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        std::string expected(1024 * 64 + 1, 'b');
        EXPECT_EQ(expected, written);

        return {static_cast<ssize_t>(num_bytes), 0};
      }));

  std::string big_string(1024 * 64 + 1, 'b');
  stats_file->write(big_string);

  {
    Thread::LockGuard lock(file->write_mutex_);
    while (file->num_writes_ != 2) {
      file->write_event_.wait(file->write_mutex_);
    }
  }
}
} // namespace Envoy
