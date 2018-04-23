#include <chrono>
#include <string>

#include "common/api/os_sys_calls_impl.h"
#include "common/common/thread.h"
#include "common/event/dispatcher_impl.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::Throw;
using testing::_;

namespace Envoy {

TEST(FileSystemImpl, BadFile) {
  Event::MockDispatcher dispatcher;
  Thread::MutexBasicLockable lock;
  Stats::IsolatedStoreImpl store;
  EXPECT_CALL(dispatcher, createTimer_(_));
  EXPECT_THROW(Filesystem::FileImpl("", dispatcher, lock, store, std::chrono::milliseconds(10000)),
               EnvoyException);
}

TEST(FileSystemImpl, fileExists) {
  EXPECT_TRUE(Filesystem::fileExists("/dev/null"));
  EXPECT_FALSE(Filesystem::fileExists("/dev/blahblahblah"));
}

TEST(FileSystemImpl, directoryExists) {
  EXPECT_TRUE(Filesystem::directoryExists("/dev"));
  EXPECT_FALSE(Filesystem::directoryExists("/dev/null"));
  EXPECT_FALSE(Filesystem::directoryExists("/dev/blahblah"));
}

TEST(FileSystemImpl, fileSize) {
  EXPECT_EQ(0, Filesystem::fileSize("/dev/null"));
  EXPECT_EQ(-1, Filesystem::fileSize("/dev/blahblahblah"));
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);
  EXPECT_EQ(data.length(), Filesystem::fileSize(file_path));
}

TEST(FileSystemImpl, fileReadToEndSuccess) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  EXPECT_EQ(data, Filesystem::fileReadToEnd(file_path));
}

// Files are read into std::string; verify that all bytes (eg non-ascii characters) come back
// unmodified
TEST(FileSystemImpl, fileReadToEndSuccessBinary) {
  std::string data;
  for (unsigned i = 0; i < 256; i++) {
    data.push_back(i);
  }
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  const std::string read = Filesystem::fileReadToEnd(file_path);
  const std::vector<uint8_t> binary_read(read.begin(), read.end());
  EXPECT_EQ(binary_read.size(), 256);
  for (unsigned i = 0; i < 256; i++) {
    EXPECT_EQ(binary_read.at(i), i);
  }
}

TEST(FileSystemImpl, fileReadToEndDoesNotExist) {
  unlink(TestEnvironment::temporaryPath("envoy_this_not_exist").c_str());
  EXPECT_THROW(Filesystem::fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist")),
               EnvoyException);
}

TEST(FilesystemImpl, CanonicalPathSuccess) { EXPECT_EQ("/", Filesystem::canonicalPath("//")); }

TEST(FilesystemImpl, CanonicalPathFail) {
  EXPECT_THROW_WITH_MESSAGE(Filesystem::canonicalPath("/_some_non_existant_file"), EnvoyException,
                            "Unable to determine canonical path for /_some_non_existant_file");
}

TEST(FilesystemImpl, IllegalPath) {
  EXPECT_FALSE(Filesystem::illegalPath("/"));
  EXPECT_TRUE(Filesystem::illegalPath("/dev"));
  EXPECT_TRUE(Filesystem::illegalPath("/dev/"));
  EXPECT_TRUE(Filesystem::illegalPath("/proc"));
  EXPECT_TRUE(Filesystem::illegalPath("/proc/"));
  EXPECT_TRUE(Filesystem::illegalPath("/sys"));
  EXPECT_TRUE(Filesystem::illegalPath("/sys/"));
  EXPECT_TRUE(Filesystem::illegalPath("/_some_non_existant_file"));
}

TEST(FileSystemImpl, flushToLogFilePeriodically) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, open_(_, _, _)).WillOnce(Return(5));
  Filesystem::FileImpl file("", dispatcher, mutex, stats_store, std::chrono::milliseconds(40));

  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40)));
  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test", written);
        EXPECT_EQ(5, fd);

        return num_bytes;
      }));

  file.write("test");

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 1) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test2", written);
        EXPECT_EQ(5, fd);

        return num_bytes;
      }));

  // make sure timer is re-enabled on callback call
  file.write("test2");
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40)));
  timer->callback_();

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 2) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }
}

TEST(FileSystemImpl, flushToLogFileOnDemand) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, open_(_, _, _)).WillOnce(Return(5));
  Filesystem::FileImpl file("", dispatcher, mutex, stats_store, std::chrono::milliseconds(40));

  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40)));

  // The first write to a given file will start the flush thread, which can flush
  // immediately (race on whether it will or not). So do a write and flush to
  // get that state out of the way, then test that small writes don't trigger a flush.
  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int, const void*, size_t num_bytes) -> ssize_t { return num_bytes; }));
  file.write("prime-it");
  file.flush();
  uint32_t expected_writes = 1;
  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    EXPECT_EQ(expected_writes, os_sys_calls.num_writes_);
  }

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test", written);
        EXPECT_EQ(5, fd);

        return num_bytes;
      }));

  file.write("test");

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    EXPECT_EQ(expected_writes, os_sys_calls.num_writes_);
  }

  file.flush();
  expected_writes++;
  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    EXPECT_EQ(expected_writes, os_sys_calls.num_writes_);
  }

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("test2", written);
        EXPECT_EQ(5, fd);

        return num_bytes;
      }));

  // make sure timer is re-enabled on callback call
  file.write("test2");
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(40)));
  timer->callback_();
  expected_writes++;

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != expected_writes) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }
}

TEST(FileSystemImpl, reopenFile) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Sequence sq;
  EXPECT_CALL(os_sys_calls, open_(_, _, _)).InSequence(sq).WillOnce(Return(5));
  Filesystem::FileImpl file("", dispatcher, mutex, stats_store, std::chrono::milliseconds(40));

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .InSequence(sq)
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("before", written);
        EXPECT_EQ(5, fd);

        return num_bytes;
      }));

  file.write("before");
  timer->callback_();

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 1) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }

  EXPECT_CALL(os_sys_calls, close(5)).InSequence(sq);
  EXPECT_CALL(os_sys_calls, open_(_, _, _)).InSequence(sq).WillOnce(Return(10));

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .InSequence(sq)
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        EXPECT_EQ("reopened", written);
        EXPECT_EQ(10, fd);

        return num_bytes;
      }));

  EXPECT_CALL(os_sys_calls, close(10)).InSequence(sq);

  file.reopen();
  file.write("reopened");
  timer->callback_();

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 2) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }
}

TEST(FilesystemImpl, reopenThrows) {
  NiceMock<Event::MockDispatcher> dispatcher;
  NiceMock<Event::MockTimer>* timer = new NiceMock<Event::MockTimer>(&dispatcher);

  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillRepeatedly(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        UNREFERENCED_PARAMETER(fd);
        UNREFERENCED_PARAMETER(buffer);

        return num_bytes;
      }));

  Sequence sq;
  EXPECT_CALL(os_sys_calls, open_(_, _, _)).InSequence(sq).WillOnce(Return(5));

  Filesystem::FileImpl file("", dispatcher, mutex, stats_store, std::chrono::milliseconds(40));
  EXPECT_CALL(os_sys_calls, close(5)).InSequence(sq);
  EXPECT_CALL(os_sys_calls, open_(_, _, _)).InSequence(sq).WillOnce(Return(-1));

  file.write("test write");
  timer->callback_();
  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 1) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }
  file.reopen();

  file.write("this is to force reopen");
  timer->callback_();

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.open_mutex_);
    while (os_sys_calls.num_open_ != 2) {
      os_sys_calls.open_event_.wait(os_sys_calls.open_mutex_);
    }
  }

  // write call should not cause any exceptions
  file.write("random data");
  timer->callback_();
}

TEST(FilesystemImpl, bigDataChunkShouldBeFlushedWithoutTimer) {
  NiceMock<Event::MockDispatcher> dispatcher;
  Thread::MutexBasicLockable mutex;
  Stats::IsolatedStoreImpl stats_store;
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Filesystem::FileImpl file("", dispatcher, mutex, stats_store, std::chrono::milliseconds(40));

  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        UNREFERENCED_PARAMETER(fd);

        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        std::string expected("a");
        EXPECT_EQ(expected, written);

        return num_bytes;
      }));

  file.write("a");

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 1) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }

  // First write happens without waiting on thread_flush_. Now make a big string and it should be
  // flushed even when timer is not enabled
  EXPECT_CALL(os_sys_calls, write_(_, _, _))
      .WillOnce(Invoke([](int fd, const void* buffer, size_t num_bytes) -> ssize_t {
        UNREFERENCED_PARAMETER(fd);

        std::string written = std::string(reinterpret_cast<const char*>(buffer), num_bytes);
        std::string expected(1024 * 64 + 1, 'b');
        EXPECT_EQ(expected, written);

        return num_bytes;
      }));

  std::string big_string(1024 * 64 + 1, 'b');
  file.write(big_string);

  {
    std::unique_lock<Thread::BasicLockable> lock(os_sys_calls.write_mutex_);
    while (os_sys_calls.num_writes_ != 2) {
      os_sys_calls.write_event_.wait(os_sys_calls.write_mutex_);
    }
  }
}
} // namespace Envoy
