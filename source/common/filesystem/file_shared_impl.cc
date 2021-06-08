#include "source/common/filesystem/file_shared_impl.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Filesystem {

Api::IoError::IoErrorCode IoFileError::getErrorCode() const {
  switch (errno_) {
  case HANDLE_ERROR_PERM:
    return IoErrorCode::Permission;
  case HANDLE_ERROR_INVALID:
    return IoErrorCode::BadFd;
  default:
    ENVOY_LOG_MISC(debug, "Unknown error code {} details {}", errno_, getErrorDetails());
    return IoErrorCode::UnknownError;
  }
}

std::string IoFileError::getErrorDetails() const { return errorDetails(errno_); }

bool FileSharedImpl::isOpen() const { return fd_ != INVALID_HANDLE; };

std::string FileSharedImpl::path() const { return filepath_and_type_.path_; };

DestinationType FileSharedImpl::destinationType() const { return filepath_and_type_.file_type_; };

} // namespace Filesystem
} // namespace Envoy
