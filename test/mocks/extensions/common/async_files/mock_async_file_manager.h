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
  MOCK_METHOD(AsyncFileHandle, newFileHandle, ());
  MOCK_METHOD(unsigned int, threadPoolSize, (), (const));
  MOCK_METHOD(void, enqueue, (const std::shared_ptr<AsyncFileContextImpl>));
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
