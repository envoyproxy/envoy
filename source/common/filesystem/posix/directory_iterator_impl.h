#pragma once

#include <dirent.h>

#include "envoy/filesystem/filesystem.h"

#include "common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Filesystem {

class DirectoryIteratorImpl : public DirectoryIterator {
public:
  DirectoryIteratorImpl(const std::string& directory_path)
      : DirectoryIterator(directory_path), dir_(nullptr),
        os_sys_calls_(Api::OsSysCallsSingleton::get()) {}
  ~DirectoryIteratorImpl();

  DirectoryEntry nextEntry() override;

private:
  void openDirectory();
  FileType fileType(const std::string& full_path) const;

  DIR* dir_;
  Api::OsSysCallsImpl& os_sys_calls_;
};

} // namespace Filesystem
} // namespace Envoy