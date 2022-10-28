#include <string>
#include <vector>

#include "source/extensions/common/async_files/status_after_file_error.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

TEST(StatusAfterFileErrorTest, AllErrnosReturnErrors) {
  std::vector<int> errors = {
      EACCES,      EPERM,     EROFS,  EBADF,  EBUSY,  EISDIR, ELOOP,      ENOTDIR, ETXTBSY,
      EWOULDBLOCK, EMFILE,    ENFILE, ENOMEM, ENOSPC, EEXIST, EFAULT,     EINVAL,  ENAMETOOLONG,
      EFBIG,       EOVERFLOW, EINTR,  ENODEV, ENOENT, ENXIO,  EOPNOTSUPP, EDQUOT,
  };
  for (const int error : errors) {
    auto status = statusAfterFileError(error);
    EXPECT_NE(absl::StatusCode::kOk, status.code());
    EXPECT_NE("", status.message());
  }
}

TEST(StatusAfterFileErrorDeathTest, UnknownErrorReturnsError) {
  absl::Status status;
  EXPECT_ENVOY_BUG({ status = statusAfterFileError(784324); }, "Unrecognized error code 784324");
}

TEST(StatusAfterFileErrorTest, ErrnoZeroReturnsOK) {
  Api::SysCallSizeResult result{0, 0};
  auto status = statusAfterFileError(result);
  EXPECT_EQ(absl::OkStatus(), status);
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
