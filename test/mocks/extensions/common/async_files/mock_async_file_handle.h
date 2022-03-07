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
  MOCK_METHOD(void, abort, ());
  MOCK_METHOD(void, createAnonymousFile, (std::string, std::function<void(absl::Status)>));
  MOCK_METHOD(void, createHardLink, (std::string, std::function<void(absl::Status)>));
  MOCK_METHOD(void, unlink, (std::string, std::function<void(absl::Status)>));
  MOCK_METHOD(void, openExistingFile, (std::string, Mode, std::function<void(absl::Status)>));
  MOCK_METHOD(void, close, (std::function<void(absl::Status)> on_complete));
  MOCK_METHOD(void, read,
              (off_t, size_t,
               std::function<void(absl::StatusOr<std::unique_ptr<Envoy::Buffer::Instance>>)>));
  MOCK_METHOD(void, write,
              (Envoy::Buffer::Instance&, off_t, // NOLINT(runtime/references)
               std::function<void(absl::StatusOr<size_t>)>));
  MOCK_METHOD(void, whenReady, (std::function<void(absl::Status)>));
  MOCK_METHOD(absl::StatusOr<std::shared_ptr<AsyncFileContext>>, duplicate, ());
};

using MockAsyncFileHandle = std::shared_ptr<MockAsyncFileContext>;

inline MockAsyncFileHandle mockAsyncFileHandle() {
  return std::make_shared<MockAsyncFileContext>();
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
