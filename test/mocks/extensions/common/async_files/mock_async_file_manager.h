#pragma once

#include <memory>

#include "source/extensions/common/async_files/async_file_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class MockAsyncFileManager : public AsyncFileManager {
public:
  MOCK_METHOD(std::function<void()>, createAnonymousFile,
              (absl::string_view, std::function<void(absl::StatusOr<AsyncFileHandle>)>));
  MOCK_METHOD(std::function<void()>, openExistingFile,
              (absl::string_view, Mode, std::function<void(absl::StatusOr<AsyncFileHandle>)>));
  MOCK_METHOD(std::function<void()>, unlink,
              (absl::string_view, std::function<void(absl::Status)>));
  MOCK_METHOD(std::function<void()>, whenReady, (std::function<void(absl::Status)>));
  MOCK_METHOD(std::function<void()>, enqueue, (const std::shared_ptr<AsyncFileAction>));
  MOCK_METHOD(std::string, describe, (), (const));
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
