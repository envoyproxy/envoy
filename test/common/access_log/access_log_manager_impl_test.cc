#include <memory>

#include "source/common/access_log/access_log_manager_impl.h"
#include "source/common/filesystem/file_shared_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/access_log/mocks.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnNew;
using testing::ReturnRef;
using testing::Sequence;

namespace Envoy {
namespace AccessLog {
namespace {

class AccessLogManagerImplTest : public testing::Test {
protected:
  AccessLogManagerImplTest()
      : file_(new NiceMock<Filesystem::MockFile>), thread_factory_(Thread::threadFactoryForTest()),
        access_log_manager_(timeout_40ms_, api_, dispatcher_, lock_, store_) {
    EXPECT_CALL(file_system_,
                createFile(testing::Matcher<const Envoy::Filesystem::FilePathAndType&>(
                    Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"})))
        .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file_))))
        .WillRepeatedly(
            Invoke([](const Envoy::Filesystem::FilePathAndType&) -> Filesystem::FilePtr {
              NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
              EXPECT_CALL(*file, path()).WillRepeatedly(Return("foo"));
              return std::unique_ptr<NiceMock<Filesystem::MockFile>>(file);
            }));
    EXPECT_CALL(*file_, path()).WillRepeatedly(Return("foo"));
    EXPECT_CALL(api_, fileSystem()).WillRepeatedly(ReturnRef(file_system_));
    EXPECT_CALL(api_, threadFactory()).WillRepeatedly(ReturnRef(thread_factory_));
  }

  void waitForCounterEq(const std::string& name, uint64_t value) {
    TestUtility::waitForCounterEq(store_, name, value, time_system_);
  }

  void waitForGaugeEq(const std::string& name, uint64_t value) {
    TestUtility::waitForGaugeEq(store_, name, value, time_system_);
  }

  NiceMock<Api::MockApi> api_;
  NiceMock<Filesystem::MockInstance> file_system_;
  NiceMock<Filesystem::MockFile>* file_;
  const std::chrono::milliseconds timeout_40ms_{40};
  Stats::TestUtil::TestStore store_;
  Thread::ThreadFactory& thread_factory_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  Thread::MutexBasicLockable lock_;
  AccessLogManagerImpl access_log_manager_;
  Event::TestRealTimeSystem time_system_;
};

TEST_F(AccessLogManagerImplTest, BadFile) {
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*file_, open_(_)).WillOnce(Return(ByMove(Filesystem::resultFailure<bool>(false, 0))));
  EXPECT_THROW(access_log_manager_.createAccessLog(
                   Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"}),
               EnvoyException);
}

TEST_F(AccessLogManagerImplTest, OpenFileWithRightFlags) {
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*file_, open_(_))
      .WillOnce(Invoke([](Filesystem::FlagSet flags) -> Api::IoCallBoolResult {
        EXPECT_FALSE(flags[Filesystem::File::Operation::Read]);
        EXPECT_TRUE(flags[Filesystem::File::Operation::Write]);
        EXPECT_TRUE(flags[Filesystem::File::Operation::Append]);
        EXPECT_TRUE(flags[Filesystem::File::Operation::Create]);
        return Filesystem::resultSuccess<bool>(true);
      }));
  EXPECT_NE(nullptr, access_log_manager_.createAccessLog(
                         Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"}));
  EXPECT_CALL(*file_, close_()).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

TEST_F(AccessLogManagerImplTest, FlushToLogFilePeriodically) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, open_(_)).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_, _));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_EQ(0UL, store_.counter("filesystem.write_failed").value());
  EXPECT_EQ(0UL, store_.counter("filesystem.write_completed").value());
  EXPECT_EQ(0UL, store_.counter("filesystem.flushed_by_timer").value());
  EXPECT_EQ(0UL, store_.counter("filesystem.write_buffered").value());

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([&](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(
            4UL,
            store_.gauge("filesystem.write_total_buffered", Stats::Gauge::ImportMode::Accumulate)
                .value());
        EXPECT_EQ(0, data.compare("test"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("test");

  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));

  waitForCounterEq("filesystem.write_completed", 1);
  EXPECT_EQ(1UL, store_.counter("filesystem.write_buffered").value());
  EXPECT_EQ(0UL, store_.counter("filesystem.flushed_by_timer").value());
  waitForGaugeEq("filesystem.write_total_buffered", 0);

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([&](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(
            5UL,
            store_.gauge("filesystem.write_total_buffered", Stats::Gauge::ImportMode::Accumulate)
                .value());
        EXPECT_EQ(0, data.compare("test2"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("test2");
  EXPECT_EQ(2UL, store_.counter("filesystem.write_buffered").value());

  // make sure timer is re-enabled on callback call
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_, _));
  timer->invokeCallback();

  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 2));

  waitForCounterEq("filesystem.write_completed", 2);
  EXPECT_EQ(0UL, store_.counter("filesystem.write_failed").value());
  EXPECT_EQ(1UL, store_.counter("filesystem.flushed_by_timer").value());
  EXPECT_EQ(2UL, store_.counter("filesystem.write_buffered").value());
  waitForGaugeEq("filesystem.write_total_buffered", 0);

  EXPECT_CALL(*file_, close_()).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

TEST_F(AccessLogManagerImplTest, FlushToLogFileOnDemand) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, open_(_)).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_, _));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_EQ(0UL, store_.counter("filesystem.flushed_by_timer").value());

  // The first write to a given file will start the flush thread. Because AccessManagerImpl::write
  // holds the write_lock_ when the thread is started, the thread will flush on its first loop, once
  // it obtains the write_lock_. Perform a write to get all that out of the way.
  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));
  log_file->write("prime-it");
  uint32_t expected_writes = 1;
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, expected_writes));

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("test"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("test");

  {
    absl::MutexLock lock(&file_->mutex_);
    EXPECT_EQ(expected_writes, file_->num_writes_);
  }

  log_file->flush();
  expected_writes++;
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, expected_writes));

  waitForCounterEq("filesystem.write_completed", 2);
  EXPECT_EQ(0UL, store_.counter("filesystem.flushed_by_timer").value());

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("test2"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  // make sure timer is re-enabled on callback call
  log_file->write("test2");
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_, _));
  timer->invokeCallback();
  expected_writes++;

  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, expected_writes));
  EXPECT_CALL(*file_, close_()).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

TEST_F(AccessLogManagerImplTest, FlushCountsIOErrors) {
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  EXPECT_CALL(*file_, open_(_)).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*timer, enableTimer(timeout_40ms_, _));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_EQ(0UL, store_.counter("filesystem.write_failed").value());

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("test"));
        return Filesystem::resultFailure<ssize_t>(2UL, ENOSPC);
      }));

  log_file->write("test");

  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));
  waitForCounterEq("filesystem.write_failed", 1);
  EXPECT_EQ(0UL, store_.counter("filesystem.write_completed").value());

  EXPECT_CALL(*file_, close_()).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

TEST_F(AccessLogManagerImplTest, ReopenFile) {
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  Sequence sq;
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("before"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("before");
  timer->invokeCallback();
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("reopened"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  log_file->reopen();
  log_file->write("reopened");
  timer->invokeCallback();

  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 2));
  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 2));
}

// Test that the `reopen()` will trigger file reopen even if no data is waiting.
TEST_F(AccessLogManagerImplTest, ReopenFileNoWrite) {
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  Sequence sq;
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("before"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("before");
  timer->invokeCallback();
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  log_file->reopen();

  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 2));
}

TEST_F(AccessLogManagerImplTest, ReopenRetry) {
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher_);

  Sequence sq;
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, write_(_))
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("before reopen"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  log_file->write("before reopen");
  timer->invokeCallback();
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, open_(_))
      .Times(3)
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultFailure<bool>(false, 0))))
      .WillOnce(Return(ByMove(Filesystem::resultFailure<bool>(false, 0))))
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, write_(_))
      .Times(2)
      .InSequence(sq)
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(data, "retry reopen");
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(data, "after reopen");
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  log_file->reopen();

  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 2));

  // Retry the reopen by calling `reopen()` another time.
  // This time is also set to fail.
  log_file->reopen();
  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 3));

  // Retry the reopen by writing more data and running the timer.
  log_file->write("retry reopen");
  timer->invokeCallback();
  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 4));
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 2));

  log_file->write("after reopen");
  timer->invokeCallback();

  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 3));
  waitForCounterEq("filesystem.reopen_failed", 2);
  waitForGaugeEq("filesystem.write_total_buffered", 0);
}

TEST_F(AccessLogManagerImplTest, BigDataChunkShouldBeFlushedWithoutTimer) {
  EXPECT_CALL(*file_, open_(_)).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  AccessLogFileSharedPtr log_file = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_EQ(0, data.compare("a"));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  log_file->write("a");
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 1));

  // First write happens without waiting on thread_flush_. Now make a big string and it should be
  // flushed even when timer is not enabled
  EXPECT_CALL(*file_, write_(_))
      .WillOnce(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        std::string expected(1024 * 64 + 1, 'b');
        EXPECT_EQ(0, data.compare(expected));
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  std::string big_string(1024 * 64 + 1, 'b');
  log_file->write(big_string);
  EXPECT_TRUE(file_->waitForEventCount(file_->num_writes_, 2));

  EXPECT_CALL(*file_, close_()).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

TEST_F(AccessLogManagerImplTest, ReopenAllFiles) {
  EXPECT_CALL(dispatcher_, createTimer_(_)).WillRepeatedly(ReturnNew<NiceMock<Event::MockTimer>>());

  Sequence sq;
  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  AccessLogFileSharedPtr log = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"});

  NiceMock<Filesystem::MockFile>* file2 = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(*file2, path()).WillRepeatedly(Return("bar"));
  EXPECT_CALL(file_system_,
              createFile(testing::Matcher<const Envoy::Filesystem::FilePathAndType&>(
                  Filesystem::FilePathAndType{Filesystem::DestinationType::File, "bar"})))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file2))))
      .WillRepeatedly(Invoke([](const Envoy::Filesystem::FilePathAndType&) -> Filesystem::FilePtr {
        NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
        EXPECT_CALL(*file, path()).WillRepeatedly(Return("bar"));
        return std::unique_ptr<NiceMock<Filesystem::MockFile>>(file);
      }));

  Sequence sq2;
  EXPECT_CALL(*file2, open_(_))
      .InSequence(sq2)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  AccessLogFileSharedPtr log2 = access_log_manager_.createAccessLog(
      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "bar"});

  // Make sure that getting the access log with the same name returns the same underlying file.
  EXPECT_EQ(log, access_log_manager_.createAccessLog(
                     Filesystem::FilePathAndType{Filesystem::DestinationType::File, "foo"}));
  EXPECT_EQ(log2, access_log_manager_.createAccessLog(
                      Filesystem::FilePathAndType{Filesystem::DestinationType::File, "bar"}));

  // Test that reopen reopens all of the files
  EXPECT_CALL(*file_, write_(_))
      .WillRepeatedly(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  EXPECT_CALL(*file2, write_(_))
      .WillRepeatedly(Invoke([](absl::string_view data) -> Api::IoCallSizeResult {
        return Filesystem::resultSuccess<ssize_t>(static_cast<ssize_t>(data.length()));
      }));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file2, close_())
      .InSequence(sq2)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  EXPECT_CALL(*file_, open_(_))
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file2, open_(_))
      .InSequence(sq2)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));

  access_log_manager_.reopen();

  log->write("this is to force reopen");
  log2->write("this is to force reopen");

  EXPECT_TRUE(file_->waitForEventCount(file_->num_opens_, 2));
  EXPECT_TRUE(file2->waitForEventCount(file2->num_opens_, 2));

  EXPECT_CALL(*file_, close_())
      .InSequence(sq)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file2, close_())
      .InSequence(sq2)
      .WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
}

} // namespace
} // namespace AccessLog
} // namespace Envoy
