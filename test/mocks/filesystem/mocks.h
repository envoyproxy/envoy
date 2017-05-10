#pragma once

#include <condition_variable>
#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/common/thread.h"

#include "gmock/gmock.h"

namespace Envoy {
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
  MockFile();
  ~MockFile();

  // Filesystem::File
  MOCK_METHOD1(write, void(const std::string& data));
  MOCK_METHOD0(reopen, void());
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher();

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // Filesystem
} // Envoy
