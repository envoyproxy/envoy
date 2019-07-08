#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"
#include "envoy/filesystem/watcher.h"

#include "common/common/thread.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Filesystem {

class MockFile : public File {
public:
  MockFile();
  ~MockFile() override;

  // Filesystem::File
  Api::IoCallBoolResult open() override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  bool isOpen() const override { return is_open_; };
  MOCK_CONST_METHOD0(path, std::string());

  MOCK_METHOD0(open_, Api::IoCallBoolResult());
  MOCK_METHOD1(write_, Api::IoCallSizeResult(absl::string_view buffer));
  MOCK_METHOD0(close_, Api::IoCallBoolResult());

  size_t num_opens_;
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
  ~MockInstance() override;

  // Filesystem::Instance
  MOCK_METHOD1(createFile, FilePtr(const std::string&));
  MOCK_METHOD1(fileExists, bool(const std::string&));
  MOCK_METHOD1(directoryExists, bool(const std::string&));
  MOCK_METHOD1(fileSize, ssize_t(const std::string&));
  MOCK_METHOD1(fileReadToEnd, std::string(const std::string&));
  MOCK_METHOD1(illegalPath, bool(const std::string&));
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher() override;

  MOCK_METHOD3(addWatch, void(const std::string&, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
