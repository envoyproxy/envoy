#pragma once

#include <string>

#include "envoy/filesystem/filesystem.h"

#include "common/filesystem/directory_iterator_impl.h"

namespace Envoy {
namespace Filesystem {

// This class does not do any validation of input data. Do not use with untrusted inputs.
class Directory {
public:
  Directory(const std::string& directory_path) : directory_path_(directory_path) {}

  DirectoryIteratorImpl begin() { return DirectoryIteratorImpl(directory_path_); }

  DirectoryIteratorImpl end() { return DirectoryIteratorImpl(); }

private:
  const std::string directory_path_;
};

} // namespace Filesystem
} // namespace Envoy
