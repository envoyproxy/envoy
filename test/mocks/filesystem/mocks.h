#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/common/thread.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Filesystem {

class MockFile : public File {
public:
  MockFile();
  ~MockFile();

  // Filesystem::File
  Api::SysCallSizeResult write(const void* buffer, size_t len) override;
  void open() override;
  void close() override;
  bool isOpen() override { return is_open_; };
  MOCK_METHOD0(open_, void());
  MOCK_METHOD2(write_, Api::SysCallSizeResult(const void* buffer, size_t len));
  MOCK_METHOD0(close_, void());

  size_t num_open_;
  size_t num_writes_;
  Thread::MutexBasicLockable open_mutex_;
  Thread::MutexBasicLockable write_mutex_;
  Thread::CondVar open_event_;
  Thread::CondVar write_event_;

private:
  bool is_open_;
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Filesystem::Instance
  MOCK_METHOD1(fileExists, bool(const std::string&));
  MOCK_METHOD1(directoryExists, bool(const std::string&));
  MOCK_METHOD1(fileSize, ssize_t(const std::string&));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string&));
  MOCK_METHOD1(illegalPath, bool(const std::string&));
  MOCK_METHOD1(createFile, FilePtr(const std::string&));
};

class MockStatsFile : public StatsFile {
public:
  MockStatsFile();
  ~MockStatsFile();

  // Filesystem::StatsFile
  MOCK_METHOD1(write, void(absl::string_view data));
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD0(flush, void());
};

class MockStatsInstance : public StatsInstance {
public:
  MockStatsInstance();
  ~MockStatsInstance();

  // Filesystem::StatsInstance
  MOCK_METHOD4(createStatsFile,
               StatsFileSharedPtr(const std::string&, Event::Dispatcher&, Thread::BasicLockable&,
                                  std::chrono::milliseconds));
  MOCK_METHOD3(createStatsFile,
               StatsFileSharedPtr(const std::string&, Event::Dispatcher&, Thread::BasicLockable&));

  // Filesystem::Instance
  MOCK_METHOD1(fileExists, bool(const std::string&));
  MOCK_METHOD1(directoryExists, bool(const std::string&));
  MOCK_METHOD1(fileSize, ssize_t(const std::string&));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string&));
  MOCK_METHOD1(illegalPath, bool(const std::string&));
  MOCK_METHOD1(createFile, FilePtr(const std::string&));
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher();

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
