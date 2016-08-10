#pragma once

#include "envoy/api/api.h"
#include "envoy/event/dispatcher.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/stats/stats.h"

#include "common/common/thread.h"

namespace Filesystem {

class MockOsSysCalls : public OsSysCalls {
public:
  MockOsSysCalls();
  ~MockOsSysCalls();

  // Filesystem::OsSysCalls
  ssize_t write(int fd, const void* buffer, size_t num_bytes) override;
  int open(const std::string& full_path, int flags, int mode) override;
  MOCK_METHOD1(close, int(int));

  MOCK_METHOD3(open_, int(const std::string& full_path, int flags, int mode));
  MOCK_METHOD3(write_, ssize_t(int, const void*, size_t));

  size_t num_writes_;
  size_t num_open_;
  Thread::MutexBasicLockable write_mutex_;
  Thread::MutexBasicLockable open_mutex_;
  std::condition_variable_any write_event_;
  std::condition_variable_any open_event_;
};

class MockFile : public File {
public:
  // Filesystem::File
  MOCK_METHOD1(write, void(const std::string& data));
  MOCK_METHOD0(reopen, void());
};

} // Filesystem

namespace Api {

class MockApi : public Api {
public:
  MockApi();
  ~MockApi();

  // Api::Api
  Event::DispatcherPtr allocateDispatcher() override {
    return Event::DispatcherPtr{allocateDispatcher_()};
  }

  Filesystem::FilePtr createFile(const std::string& path, Event::Dispatcher& dispatcher,
                                 Thread::BasicLockable& lock, Stats::Store& stats_store) override {
    return Filesystem::FilePtr{createFile_(path, dispatcher, lock, stats_store)};
  }

  MOCK_METHOD0(allocateDispatcher_, Event::Dispatcher*());
  MOCK_METHOD4(createFile_,
               Filesystem::File*(const std::string& path, Event::Dispatcher& dispatcher,
                                 Thread::BasicLockable& lock, Stats::Store& stats_store));
  MOCK_METHOD1(fileExists, bool(const std::string& path));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string& path));
};

} // Api
