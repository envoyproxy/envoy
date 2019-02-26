#include "common/filesystem/io_file_error.h"

#include <cstring>

namespace Envoy {
namespace Filesystem {

Api::IoError::IoErrorCode IoFileError::errorCode() const {
  switch (errno_) {
  case EBADF:
    return IoErrorCode::BadHandle;
  default:
    return IoErrorCode::UnknownError;
  }
}

std::string IoFileError::errorDetails() const { return ::strerror(errno_); }

} // namespace Filesystem
} // namespace Envoy