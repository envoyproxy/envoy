#include <memory>

#include "common/access_log/access_log_manager_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::Sequence;

namespace Envoy {
namespace AccessLog {

class AccessLogManagerImplTest : public testing::Test {
protected:
  AccessLogManagerImplTest()
      : file_(new NiceMock<Filesystem::MockFile>), thread_factory_(Thread::threadFactoryForTest()),
        access_log_manager_(timeout_40ms_, api_, dispatcher_, lock_, store_) {
    EXPECT_CALL(file_system_, createFile("foo"))
        .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file_))));

    EXPECT_CALL(api_, fileSystem()).WillRepeatedly(ReturnRef(file_system_));
    EXPECT_CALL(api_, threadFactory()).WillRepeatedly(ReturnRef(thread_factory_));
  }

  NiceMock<Api::MockApi> api_;
  NiceMock<Filesystem::MockInstance> file_system_;
  NiceMock<Filesystem::MockFile>* file_;
  const std::chrono::milliseconds timeout_40ms_{40};
  Stats::IsolatedStoreImpl store_;
  Thread::ThreadFactory& thread_factory_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Thread::MutexBasicLockable lock_;
  AccessLogManagerImpl access_log_manager_;
};

TEST_F(AccessLogManagerImplTest, BadFile) {
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*file_, open_()).WillOnce(Return(false));
  EXPECT_THROW(access_log_manager_.createAccessLog("foo"), EnvoyException);
}

TEST_F(AccessLogManagerImplTest, flushToLogFilePeriodically) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, open_()).WillOnce(Return(true));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog("foo");

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    EXPECT_EQ(0, data.compare("test"));
    return static_cast<ssize_t>(data.length());
  }));

  log_file->write("test");

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 1) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }

  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    EXPECT_EQ(0, data.compare("test2"));
    return static_cast<ssize_t>(data.length());
  }));

  // make sure timer is re-enabled on callback call
  log_file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 2) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }
  EXPECT_CALL(*file_, close_()).WillOnce(Return(true));
}

TEST_F(AccessLogManagerImplTest, flushToLogFileOnDemand) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, open_()).WillOnce(Return(true));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog("foo");

  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));

  // The first write to a given file will start the flush thread, which can flush
  // immediately (race on whether it will or not). So do a write and flush to
  // get that state out of the way, then test that small writes don't trigger a flush.
  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    return static_cast<ssize_t>(data.length());
  }));
  log_file->write("prime-it");
  log_file->flush();
  uint32_t expected_writes = 1;
  {
    Thread::LockGuard lock(file_->write_mutex_);
    EXPECT_EQ(expected_writes, file_->num_writes_);
  }

  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    EXPECT_EQ(0, data.compare("test"));
    return static_cast<ssize_t>(data.length());
  }));

  log_file->write("test");

  {
    Thread::LockGuard lock(file_->write_mutex_);
    EXPECT_EQ(expected_writes, file_->num_writes_);
  }

  log_file->flush();
  expected_writes++;
  {
    Thread::LockGuard lock(file_->write_mutex_);
    EXPECT_EQ(expected_writes, file_->num_writes_);
  }

  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    EXPECT_EQ(0, data.compare("test2"));
    return static_cast<ssize_t>(data.length());
  }));

  // make sure timer is re-enabled on callback call
  log_file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_));
  timer->callback_();
  expected_writes++;

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != expected_writes) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }
  EXPECT_CALL(*file_, close_()).WillOnce(Return(true));
}

TEST_F(AccessLogManagerImplTest, reopenFile) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  Sequence sq;
  EXPECT_CALL(*file_, open_()).InSequence(sq).WillOnce(Return(true));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog("foo");

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> ssize_t {
        EXPECT_EQ(0, data.compare("before"));
        return static_cast<ssize_t>(data.length());
      }));

  log_file->write("before");
  timer->callback_();

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 1) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }

  EXPECT_CALL(*file_, close_()).InSequence(sq).WillOnce(Return(true));
  EXPECT_CALL(*file_, open_()).InSequence(sq).WillOnce(Return(true));

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> ssize_t {
        EXPECT_EQ(0, data.compare("reopened"));
        return static_cast<ssize_t>(data.length());
      }));

  EXPECT_CALL(*file_, close_()).InSequence(sq).WillOnce(Return(true));

  log_file->reopen();
  log_file->write("reopened");
  timer->callback_();

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 2) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }
}

TEST_F(AccessLogManagerImplTest, reopenThrows) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, write_(_)).WillRepeatedly(Invoke([](absl::string_view data) -> ssize_t {
    return static_cast<ssize_t>(data.length());
  }));

  Sequence sq;
  EXPECT_CALL(*file_, open_()).InSequence(sq).WillOnce(Return(true));

  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog("foo");
  EXPECT_CALL(*file_, close_()).InSequence(sq).WillOnce(Return(true));
  EXPECT_CALL(*file_, open_()).InSequence(sq).WillOnce(Return(false));

  log_file->write("test write");
  timer->callback_();
  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 1) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }
  log_file->reopen();

  log_file->write("this is to force reopen");
  timer->callback_();

  {
    Thread::LockGuard lock(file_->open_mutex_);
    while (file_->num_opens_ != 2) {
      file_->open_event_.wait(file_->open_mutex_);
    }
  }

  // write call should not cause any exceptions
  log_file->write("random data");
  timer->callback_();
}

TEST_F(AccessLogManagerImplTest, bigDataChunkShouldBeFlushedWithoutTimer) {
  EXPECT_CALL(*file_, open_()).WillOnce(Return(true));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog("foo");

  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    EXPECT_EQ(0, data.compare("a"));
    return static_cast<ssize_t>(data.length());
  }));

  log_file->write("a");

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 1) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }

  // First write happens without waiting on thread_flush_. Now make a big string and it should be
  // flushed even when timer is not enabled
  EXPECT_CALL(*file_, write_(_)).WillOnce(Invoke([](absl::string_view data) -> ssize_t {
    std::string expected(1024 * 64 + 1, 'b');
    EXPECT_EQ(0, data.compare(expected));
    return static_cast<ssize_t>(data.length());
  }));

  std::string big_string(1024 * 64 + 1, 'b');
  log_file->write(big_string);

  {
    Thread::LockGuard lock(file_->write_mutex_);
    while (file_->num_writes_ != 2) {
      file_->write_event_.wait(file_->write_mutex_);
    }
  }
  EXPECT_CALL(*file_, close_()).WillOnce(Return(true));
}

TEST_F(AccessLogManagerImplTest, reopenAllFiles) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillRepeatedly(ReturnNew<NiceMock<Event::MockTimer>>());

  EXPECT_CALL(*file_, open_()).Times(2).WillRepeatedly(Return(true));
  AccessLogFileSharedPtr log = access_log_manager_.createAccessLog("foo");

  NiceMock<Filesystem::MockFile>* file2 = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system_, createFile("bar"))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file2))));
  EXPECT_CALL(*file2, open_()).Times(2).WillRepeatedly(Return(true));
  AccessLogFileSharedPtr log2 = access_log_manager_.createAccessLog("bar");

  // Make sure that getting the access log with the same name returns the same underlying file.
  EXPECT_EQ(log, access_log_manager_.createAccessLog("foo"));
  EXPECT_EQ(log2, access_log_manager_.createAccessLog("bar"));

  EXPECT_CALL(*file_, close_()).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(*file2, close_()).Times(2).WillRepeatedly(Return(true));

  // Test that reopen reopens all of the files
  EXPECT_CALL(*file_, write_(_)).WillRepeatedly(Invoke([](absl::string_view data) -> ssize_t {
    return static_cast<ssize_t>(data.length());
  }));

  EXPECT_CALL(*file2, write_(_)).WillRepeatedly(Invoke([](absl::string_view data) -> ssize_t {
    return static_cast<ssize_t>(data.length());
  }));

  access_log_manager_.reopen();

  log->write("this is to force reopen");
  log2->write("this is to force reopen");

  {
    Thread::LockGuard lock(file_->open_mutex_);
    while (file_->num_opens_ != 2) {
      file_->open_event_.wait(file_->open_mutex_);
    }
  }

  {
    Thread::LockGuard lock(file2->open_mutex_);
    while (file2->num_opens_ != 2) {
      file2->open_event_.wait(file2->open_mutex_);
    }
  }
}

} // namespace AccessLog
} // namespace Envoy
