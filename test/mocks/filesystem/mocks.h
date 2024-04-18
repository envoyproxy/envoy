#pragma once

#include <cstdint>
#include <string>

#include "envoy/filesystem/filesystem.h"
#include "envoy/filesystem/watcher.h"

#include "source/common/common/thread.h"

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
  Api::IoCallSizeResult pread(void* buf, uint64_t count, uint64_t offset) override;
  Api::IoCallSizeResult pwrite(const void* buf, uint64_t count, uint64_t offset) override;
  bool isOpen() const override { return is_open_; };
  MOCK_METHOD(std::string, path, (), (const));
  MOCK_METHOD(DestinationType, destinationType, (), (const));
  MOCK_METHOD(Api::IoCallResult<FileInfo>, info, ());

  // The first parameter here must be `const FlagSet&` otherwise it doesn't compile with libstdc++
  MOCK_METHOD(Api::IoCallBoolResult, open_, (const FlagSet& flag));
  MOCK_METHOD(Api::IoCallSizeResult, write_, (absl::string_view buffer));
  MOCK_METHOD(Api::IoCallBoolResult, close_, ());
  MOCK_METHOD(Api::IoCallSizeResult, pread_, (void* buf, uint64_t count, uint64_t offset));
  MOCK_METHOD(Api::IoCallSizeResult, pwrite_, (const void* buf, uint64_t count, uint64_t offset));

  absl::Mutex mutex_;
  uint64_t num_opens_{0};
  uint64_t num_writes_{0};
  uint64_t num_preads_{0};
  uint64_t num_pwrites_{0};

  bool waitForEventCount(uint64_t& event_counter, uint64_t expected) {
    auto func = [&]() { return event_counter == expected; };
    bool result = mutex_.LockWhenWithTimeout(absl::Condition(&func), absl::Seconds(15));
    mutex_.Unlock();
    return result;
  }

private:
  bool is_open_{false};
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance() override;

  // Filesystem::Instance
  MOCK_METHOD(FilePtr, createFile, (const FilePathAndType&));
  MOCK_METHOD(bool, fileExists, (const std::string&));
  MOCK_METHOD(bool, directoryExists, (const std::string&));
  MOCK_METHOD(ssize_t, fileSize, (const std::string&));
  MOCK_METHOD(absl::StatusOr<std::string>, fileReadToEnd, (const std::string&));
  MOCK_METHOD(absl::StatusOr<PathSplitResult>, splitPathFromFilename, (absl::string_view));
  MOCK_METHOD(bool, illegalPath, (const std::string&));
  MOCK_METHOD(Api::IoCallResult<FileInfo>, stat, (absl::string_view));
  MOCK_METHOD(Api::IoCallBoolResult, createPath, (absl::string_view));
};

class MockWatcher : public Watcher {
public:
  MockWatcher();
  ~MockWatcher() override;

  MOCK_METHOD(absl::Status, addWatch, (absl::string_view, uint32_t, OnChangedCb));
};

} // namespace Filesystem
} // namespace Envoy
