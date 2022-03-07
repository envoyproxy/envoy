#include <string>
#include <vector>

#include "source/extensions/common/async_files/status_after_file_error.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

TEST(StatusAfterFileError, AllErrnosReturnErrors) {
  std::vector<int> errors = {
      EACCES,       EPERM,  EROFS,     EBADF,  EBUSY,  EISDIR, ELOOP,  ENOTDIR,    ETXTBSY,
      EWOULDBLOCK,  EDQUOT, EMFILE,    ENFILE, ENOMEM, ENOSPC, EEXIST, EFAULT,     EINVAL,
      ENAMETOOLONG, EFBIG,  EOVERFLOW, EINTR,  ENODEV, ENOENT, ENXIO,  EOPNOTSUPP, 784324,
  };
  for (const int error : errors) {
    errno = error;
    auto status = statusAfterFileError();
    EXPECT_NE(absl::StatusCode::kOk, status.code());
    EXPECT_NE("", status.message());
  }
} // namespace async_files

TEST(StatusAfterFileError, ErrnoZeroReturnsOK) {
  errno = 0;
  auto status = statusAfterFileError();
  EXPECT_EQ(absl::StatusCode::kOk, status.code());
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
