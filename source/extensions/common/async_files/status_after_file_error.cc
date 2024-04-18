#include "source/extensions/common/async_files/status_after_file_error.h"

#include <cerrno>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace AsyncFiles {

absl::Status statusAfterFileError(int code) {
  const std::string str = Envoy::errorDetails(code);
  switch (code) {
  case EACCES:
  case EPERM:
  case EROFS:
    return absl::PermissionDeniedError(str);
  case EBADF:
  case EBUSY:
  case EISDIR:
  case ELOOP:
  case ENOTDIR:
  case ETXTBSY:
  case EWOULDBLOCK:
    return absl::FailedPreconditionError(str);
  case EDQUOT:
  case EMFILE:
  case ENFILE:
  case ENOMEM:
  case ENOSPC:
    return absl::ResourceExhaustedError(str);
  case EEXIST:
    return absl::AlreadyExistsError(str);
  case EFAULT:
  case EINVAL:
  case ENAMETOOLONG:
    return absl::InvalidArgumentError(str);
  case EFBIG:
  case EOVERFLOW:
    return absl::OutOfRangeError(str);
  case EINTR:
    return absl::UnavailableError(str);
  case ENODEV:
  case ENOENT:
  case ENXIO:
    return absl::NotFoundError(str);
  case EOPNOTSUPP:
    return absl::UnimplementedError(str);
  case 0:
    return absl::OkStatus();
  default:
    ENVOY_BUG(false, absl::StrCat("Unrecognized error code ", code));
    return absl::UnknownError(str);
  }
}

} // namespace AsyncFiles
} // namespace Common
} // namespace Extensions
} // namespace Envoy
