#include "source/common/filesystem/file_shared_impl.h"

#include <random>

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

std::string FileSharedImpl::generateTmpFilePath(absl::string_view path) {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<> distribution('A', 'Z');
  std::array<char, 8> out;
  for (int i = 0; i < 8; i++) {
    out[i] = distribution(gen);
  }
  return absl::StrCat(path, "/envoy_", absl::string_view{&out[0], out.size()}, ".tmp");
}

} // namespace Filesystem
} // namespace Envoy
