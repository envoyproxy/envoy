#include <chrono>
#include <string>

#include "common/common/lock_guard.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

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
namespace Filesystem {

class FileSystemImplTest : public TestBase {
protected:
  FileSystemImplTest()
      : raw_file_(new NiceMock<MockRawFile>),
        file_system_(std::chrono::milliseconds(10000), Thread::threadFactoryForTest(), stats_store_,
                     raw_instance_) {
    EXPECT_CALL(raw_instance_, createRawFile(_))
        .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<MockRawFile>>(raw_file_))));
  }

  NiceMock<MockRawFile>* raw_file_;
  const std::chrono::milliseconds timeout_40ms_{40};
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<MockRawInstance> raw_instance_;
  InstanceImpl file_system_;
};

TEST_F(FileSystemImplTest, BadFile) {
  Event::MockDispatcher dispatcher;
  Thread::MutexBasicLockable lock;
  EXPECT_CALL(dispatcher, createTimer_(_));
  EXPECT_CALL(*raw_file_, open_()).WillOnce(Return(Api::SysCallBoolResult{false, 0}));
  EXPECT_THROW(file_system_.createFile("", dispatcher, lock), EnvoyException);
}

TEST_F(FileSystemImplTest, flushToLogFilePeriodically) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  Thread::MutexBasicLockable mutex;

  EXPECT_CALL(*raw_file_, open_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  FileSharedPtr file = file_system_.createFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("test"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  file->write("test");

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 1) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }

  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("test2"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  // make sure timer is re-enabled on callback call
  file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 2) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }
  EXPECT_CALL(*raw_file_, close_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
}

TEST_F(FileSystemImplTest, flushToLogFileOnDemand) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  Thread::MutexBasicLockable mutex;

  EXPECT_CALL(*raw_file_, open_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  Filesystem::FileSharedPtr file = file_system_.createFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));

  // The first write to a given file will start the flush thread, which can flush
  // immediately (race on whether it will or not). So do a write and flush to
  // get that state out of the way, then test that small writes don't trigger a flush.
  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        return {static_cast<ssize_t>(data.length()), 0};
      }));
  file->write("prime-it");
  file->flush();
  uint32_t expected_writes = 1;
  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    EXPECT_EQ(expected_writes, raw_file_->num_writes_);
  }

  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("test"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  file->write("test");

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    EXPECT_EQ(expected_writes, raw_file_->num_writes_);
  }

  file->flush();
  expected_writes++;
  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    EXPECT_EQ(expected_writes, raw_file_->num_writes_);
  }

  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("test2"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  // make sure timer is re-enabled on callback call
  file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();
  expected_writes++;

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != expected_writes) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }
  EXPECT_CALL(*raw_file_, close_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
}

TEST_F(FileSystemImplTest, reopenFile) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  Thread::MutexBasicLockable mutex;

  Sequence sq;
  EXPECT_CALL(*raw_file_, open_()).InSequence(sq).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  Filesystem::FileSharedPtr file = file_system_.createFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*raw_file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("before"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  file->write("before");
  timer->callback_();

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 1) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }

  EXPECT_CALL(*raw_file_, close_())
      .InSequence(sq)
      .WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  EXPECT_CALL(*raw_file_, open_()).InSequence(sq).WillOnce(Return(Api::SysCallBoolResult{true, 0}));

  EXPECT_CALL(*raw_file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("reopened"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  EXPECT_CALL(*raw_file_, close_())
      .InSequence(sq)
      .WillOnce(Return(Api::SysCallBoolResult{true, 0}));

  file->reopen();
  file->write("reopened");
  timer->callback_();

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 2) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }
}

TEST_F(FileSystemImplTest, reopenThrows) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);
  Thread::MutexBasicLockable mutex;

  EXPECT_CALL(*raw_file_, write_(_))
      .WillRepeatedly(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  Sequence sq;
  EXPECT_CALL(*raw_file_, open_()).InSequence(sq).WillOnce(Return(Api::SysCallBoolResult{true, 0}));

  Filesystem::FileSharedPtr file = file_system_.createFile("", dispatcher, mutex, timeout_40ms_);
  EXPECT_CALL(*raw_file_, close_())
      .InSequence(sq)
      .WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  EXPECT_CALL(*raw_file_, open_())
      .InSequence(sq)
      .WillOnce(Return(Api::SysCallBoolResult{false, 0}));

  file->write("test write");
  timer->callback_();
  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 1) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }
  file->reopen();

  file->write("this is to force reopen");
  timer->callback_();

  {
    Thread::LockGuard lock(raw_file_->open_mutex_);
    while (raw_file_->num_opens_ != 2) {
      raw_file_->open_event_.wait(raw_file_->open_mutex_);
    }
  }

  // write call should not cause any exceptions
  file->write("random data");
  timer->callback_();
}

TEST_F(FileSystemImplTest, bigDataChunkShouldBeFlushedWithoutTimer) {
  NiceMock<Event::MockDispatcher> dispatcher;
  Thread::MutexBasicLockable mutex;

  EXPECT_CALL(*raw_file_, open_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
  Filesystem::FileSharedPtr file = file_system_.createFile("", dispatcher, mutex, timeout_40ms_);

  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        EXPECT_EQ(0, data.compare("a"));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  file->write("a");

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 1) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }

  // First write happens without waiting on thread_flush_. Now make a big string and it should be
  // flushed even when timer is not enabled
  EXPECT_CALL(*raw_file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::SysCallSizeResult {
        std::string expected(1024 * 64 + 1, 'b');
        EXPECT_EQ(0, data.compare(expected));
        return {static_cast<ssize_t>(data.length()), 0};
      }));

  std::string big_string(1024 * 64 + 1, 'b');
  file->write(big_string);

  {
    Thread::LockGuard lock(raw_file_->write_mutex_);
    while (raw_file_->num_writes_ != 2) {
      raw_file_->write_event_.wait(raw_file_->write_mutex_);
    }
  }
  EXPECT_CALL(*raw_file_, close_()).WillOnce(Return(Api::SysCallBoolResult{true, 0}));
}

} // namespace Filesystem
} // namespace Envoy
