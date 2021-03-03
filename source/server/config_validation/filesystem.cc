#include "server/config_validation/filesystem.h"

#include "envoy/api/io_error.h"
#include "envoy/common/exception.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Filesystem {
namespace {

class NullFile : public File {
public:
  NullFile(std::string path, std::string error_message)
      : path_(std::move(path)), error_message_(std::move(error_message)) {}

  // File impl
  Api::IoCallBoolResult open(FlagSet) override { return {false, makeIoError()}; }
  Api::IoCallSizeResult write(absl::string_view) override { return {-1, makeIoError()}; }
  Api::IoCallBoolResult close() override { return {false, makeIoError()}; }

  bool isOpen() const override { return false; }

  std::string path() const override { return path_; }

private:
  class IoError : public Api::IoError {
  public:
    explicit IoError(std::string error_message) : error_message_(std::move(error_message)) {}
    IoErrorCode getErrorCode() const override { return IoErrorCode::NoSupport; }
    std::string getErrorDetails() const override { return error_message_; }

  private:
    const std::string error_message_;
  };

  static void freeIoError(Api::IoError* err) { std::default_delete<Api::IoError>()(err); }

  Api::IoErrorPtr makeIoError() {
    return Api::IoErrorPtr{new IoError(error_message_), &freeIoError};
  }

  const std::string path_;
  const std::string error_message_;
};
} // namespace

WrapInstance::WrapInstance(Filesystem::Instance& impl) : impl_(impl) {}

Filesystem::FilePtr WrapInstance::createFile(const std::string& path) {
  return impl_.createFile(path);
}

bool WrapInstance::fileExists(const std::string& path) { return impl_.fileExists(path); }

bool WrapInstance::directoryExists(const std::string& path) { return impl_.directoryExists(path); }

ssize_t WrapInstance::fileSize(const std::string& path) { return impl_.fileSize(path); }

std::string WrapInstance::fileReadToEnd(const std::string& path) {
  return impl_.fileReadToEnd(path);
}

Filesystem::PathSplitResult WrapInstance::splitPathFromFilename(absl::string_view path) {
  return impl_.splitPathFromFilename(path);
}

bool WrapInstance::illegalPath(const std::string& path) { return impl_.illegalPath(path); }

NullInstance::NullInstance(Filesystem::Instance& impl) : impl_(impl) {}

Filesystem::FilePtr NullInstance::createFile(const std::string& path) {
  return std::make_unique<NullFile>(path, makeErrorMessage(path));
}

bool NullInstance::fileExists(const std::string&) { return false; }

bool NullInstance::directoryExists(const std::string&) { return false; }

ssize_t NullInstance::fileSize(const std::string&) { return -1; }

std::string NullInstance::fileReadToEnd(const std::string& path) {
  throw EnvoyException(makeErrorMessage(path));
}

Filesystem::PathSplitResult NullInstance::splitPathFromFilename(absl::string_view path) {
  return impl_.splitPathFromFilename(path);
}

bool NullInstance::illegalPath(const std::string& path) { return impl_.illegalPath(path); }

std::string NullInstance::makeErrorMessage(const absl::string_view path) const {
  return absl::StrCat("null file system does not include file: ", path);
}

RestrictedInstance::RestrictedInstance(absl::string_view allowed_prefix, Filesystem::Instance& impl)
    : prefix_(allowed_prefix), impl_(impl) {}

Filesystem::FilePtr RestrictedInstance::createFile(const std::string& path) {
  if (absl::StartsWith(path, prefix_)) {
    return impl_.createFile(path);
  }
  return std::make_unique<NullFile>(path, makeErrorMessage(path));
}

bool RestrictedInstance::fileExists(const std::string& path) {
  if (absl::StartsWith(path, prefix_)) {
    return impl_.fileExists(path);
  }
  return false;
}

bool RestrictedInstance::directoryExists(const std::string& path) {
  if (absl::StartsWith(path, prefix_)) {
    return impl_.directoryExists(path);
  }
  return false;
}

ssize_t RestrictedInstance::fileSize(const std::string& path) {
  if (absl::StartsWith(path, prefix_)) {
    return impl_.fileSize(path);
  }
  return -1;
}

std::string RestrictedInstance::fileReadToEnd(const std::string& path) {
  if (absl::StartsWith(path, prefix_)) {
    return impl_.fileReadToEnd(path);
  }
  throw EnvoyException(makeErrorMessage(path));
}

Filesystem::PathSplitResult RestrictedInstance::splitPathFromFilename(absl::string_view path) {
  return impl_.splitPathFromFilename(path);
}

bool RestrictedInstance::illegalPath(const std::string& path) { return impl_.illegalPath(path); }

std::string RestrictedInstance::makeErrorMessage(const absl::string_view path) const {
  return absl::StrCat("allowed prefix ", prefix_, " does not include file: ", path);
}

InstancePtr makeValidationFilesystem(const Server::Options& options, Instance& real_file_system) {
  if (options.mode() == Server::Mode::Load) {
    if (options.configPath().empty()) {
      return std::make_unique<NullInstance>(real_file_system);
    }
    return std::make_unique<RestrictedInstance>(options.configPath(), real_file_system);
  }
  return std::make_unique<WrapInstance>(real_file_system);
}

} // namespace Filesystem
} // namespace Envoy