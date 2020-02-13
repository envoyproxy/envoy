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
  Api::IoCallBoolResult open(FlagSet flag) override;
  Api::IoCallSizeResult write(absl::string_view buffer) override;
  Api::IoCallBoolResult close() override;
  bool isOpen() const override { return is_open_; };
  MOCK_METHOD(std::string, path, (), (const));

  // The first parameter here must be `const FlagSet&` otherwise it doesn't compile with libstdc++
  MOCK_METHOD(Api::IoCallBoolResult, open_, (const FlagSet& flag));
  MOCK_METHOD(Api::IoCallSizeResult, write_, (absl::string_view buffer));
  MOCK_METHOD(Api::IoCallBoolResult, close_, ());

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
  MOCK_METHOD(FilePtr, createFile, (const std::string&));
  MOCK_METHOD(bool, fileExists, (const std::string&));
  MOCK_METHOD(bool, directoryExists, (const std::string&));
  MOCK_METHOD(ssize_t, fileSize, (const std::string&));
  MOCK_METHOD(std::string, fileReadToEnd, (const std::string&));
  MOCK_METHOD(PathSplitResult, splitPathFromFilename, (absl::string_view));
  MOCK_METHOD(bool, illegalPath, (const std::string&));
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher() override;

  MOCK_METHOD(void, addWatch, (absl::string_view, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
