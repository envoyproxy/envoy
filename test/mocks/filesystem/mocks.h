#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/common/thread.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Filesystem {

class MockRawFile : public RawFile {
public:
  MockRawFile();
  ~MockRawFile();

  // Filesystem::RawFile
  Api::SysCallBoolResult open() override;
  Api::SysCallSizeResult write(absl::string_view buffer) override;
  Api::SysCallBoolResult close() override;
  bool isOpen() override { return is_open_; };
  MOCK_METHOD0(path, std::string());
  MOCK_METHOD1(errorToString, std::string(int));

  MOCK_METHOD0(open_, Api::SysCallBoolResult());
  MOCK_METHOD1(write_, Api::SysCallSizeResult(absl::string_view buffer));
  MOCK_METHOD0(close_, Api::SysCallBoolResult());

  size_t num_opens_;
  size_t num_writes_;
  Thread::MutexBasicLockable open_mutex_;
  Thread::MutexBasicLockable write_mutex_;
  Thread::CondVar open_event_;
  Thread::CondVar write_event_;

private:
  bool is_open_;
};

class MockRawInstance : public RawInstance {
public:
  MockRawInstance();
  ~MockRawInstance();

  // Filesystem::RawInstance
  MOCK_METHOD1(createRawFile, RawFilePtr(const std::string&));
  MOCK_METHOD1(fileExists, bool(const std::string&));
  MOCK_METHOD1(directoryExists, bool(const std::string&));
  MOCK_METHOD1(fileSize, ssize_t(const std::string&));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string&));
  MOCK_METHOD1(canonicalPath, Api::SysCallStringResult(const std::string&));
  MOCK_METHOD1(illegalPath, bool(const std::string&));
};

class MockFile : public File {
public:
  MockFile();
  ~MockFile();

  // Filesystem::File
  MOCK_METHOD1(write, void(absl::string_view data));
  MOCK_METHOD0(reopen, void());
  MOCK_METHOD0(flush, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Filesystem::Instance
  MOCK_METHOD4(createFile, FileSharedPtr(const std::string&, Event::Dispatcher&,
                                         Thread::BasicLockable&, std::chrono::milliseconds));
  MOCK_METHOD3(createFile,
               FileSharedPtr(const std::string&, Event::Dispatcher&, Thread::BasicLockable&));

  // Filesystem::RawInstance
  MOCK_METHOD1(createRawFile, RawFilePtr(const std::string&));
  MOCK_METHOD1(fileExists, bool(const std::string&));
  MOCK_METHOD1(directoryExists, bool(const std::string&));
  MOCK_METHOD1(fileSize, ssize_t(const std::string&));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string&));
  MOCK_METHOD1(canonicalPath, Api::SysCallStringResult(const std::string&));
  MOCK_METHOD1(illegalPath, bool(const std::string&));
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher();

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
