#pragma once

#include <functional>
#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/async_files/async_file_handle.h"

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class MockAsyncFileContext : public AsyncFileContext {
public:
  MOCK_METHOD(std::function<void()>, createHardLink,
              (absl::string_view, std::function<void(absl::Status)>));
  MOCK_METHOD(std::function<void()>, close, (std::function<void(absl::Status)> on_complete));
  MOCK_METHOD(std::function<void()>, read,
              (off_t, size_t,
               std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)>));
  MOCK_METHOD(std::function<void()>, write,
              (Envoy::Buffer::Instance&, off_t, // NOLINT(runtime/references)
               std::function<void(absl::StatusOr<size_t>)>));
  MOCK_METHOD(std::function<void()>, duplicate,
              (std::function<void(absl::StatusOr<AsyncFileHandle>)>));
};

using MockAsyncFileHandle = std::shared_ptr<MockAsyncFileContext>;

inline MockAsyncFileHandle mockAsyncFileHandle() {
  return std::make_shared<MockAsyncFileContext>();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
