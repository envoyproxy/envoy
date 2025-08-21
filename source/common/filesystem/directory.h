#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "source/common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

// This class does not do any validation of input data. Do not use with untrusted inputs.
//
// DirectoryIteratorImpl will fail silently and act like an empty iterator in case of
// error opening the directory, and will silently skip files that can't be stat'ed. Check
// DirectoryIteratorImpl::status() after initialization and after each increment if
// silent failure is not the desired behavior.
class Directory {
public:
  Directory(const std::string& directory_path) : directory_path_(directory_path) {}

  DirectoryIteratorImpl begin() { return {directory_path_}; }

  DirectoryIteratorImpl end() { return {}; }

private:
  const std::string directory_path_;
};

} // namespace Filesystem
} // namespace Envoy
