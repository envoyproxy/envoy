#pragma once

#include <memory>

#include "source/extensions/common/async_files/posix_file_operations.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

class AsyncFileAction;

class MockPosixFileOperations : public PosixFileOperations {
public:
  MOCK_METHOD(int, open, (const char* pathname, int flags), (const));
  MOCK_METHOD(int, open, (const char* pathname, int flags, mode_t mode), (const));
  MOCK_METHOD(int, close, (int fd), (const));
  MOCK_METHOD(ssize_t, pwrite, (int fd, const void* buf, size_t count, off_t offset), (const));
  MOCK_METHOD(ssize_t, pread, (int fd, void* buf, size_t count, off_t offset), (const));
  MOCK_METHOD(int, unlink, (const char* pathname), (const));
  MOCK_METHOD(int, dup, (int fd), (const));
  MOCK_METHOD(int, linkat,
              (int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags),
              (const));
  MOCK_METHOD(int, mkstemp, (char* tmplate), (const));
};

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
