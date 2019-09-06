#pragma once

#include <dirent.h>

#include "envoy/filesystem/filesystem.h"

#include "common/api/os_sys_calls_impl.h"

namespace Envoy {
namespace Filesystem {

class DirectoryIteratorImpl : public DirectoryIterator {
public:
  DirectoryIteratorImpl(const std::string& directory_path);
  DirectoryIteratorImpl() : directory_path_(""), os_sys_calls_(Api::OsSysCallsSingleton::get()) {}

  ~DirectoryIteratorImpl() override;

  DirectoryIteratorImpl& operator++() override;

  // We don't want this iterator to be copied. If the copy gets destructed,
  // then it will close its copy of the DIR* pointer, which will cause the
  // original's to be invalid. While we could implement a deep copy constructor to
  // work around this, it is not needed the moment.
  DirectoryIteratorImpl(const DirectoryIteratorImpl&) = delete;
  DirectoryIteratorImpl(DirectoryIteratorImpl&&) = default;

private:
  void nextEntry();
  void openDirectory();
  FileType fileType(const std::string& name) const;

  std::string directory_path_;
  DIR* dir_{nullptr};
  Api::OsSysCallsImpl& os_sys_calls_;
};

} // namespace Filesystem
} // namespace Envoy
