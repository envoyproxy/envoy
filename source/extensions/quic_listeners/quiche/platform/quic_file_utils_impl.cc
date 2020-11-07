// NOLINT(namespace-envoy)

// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quic_file_utils_impl.h"

#include "common/filesystem/directory.h"
#include "common/filesystem/filesystem_impl.h"

#include "absl/strings/str_cat.h"

namespace quic {
namespace {

void depthFirstTraverseDirectory(const std::string& dirname, std::vector<std::string>& files) {
  Envoy::Filesystem::Directory directory(dirname);
  for (const Envoy::Filesystem::DirectoryEntry& entry : directory) {
    switch (entry.type_) {
    case Envoy::Filesystem::FileType::Regular:
      files.push_back(absl::StrCat(dirname, "/", entry.name_));
      break;
    case Envoy::Filesystem::FileType::Directory:
      if (entry.name_ != "." && entry.name_ != "..") {
        depthFirstTraverseDirectory(absl::StrCat(dirname, "/", entry.name_), files);
      }
      break;
    default:
      ASSERT(false,
             absl::StrCat("Unknow file entry type ", entry.type_, " under directory ", dirname));
    }
  }
}

} // namespace

// Traverses the directory |dirname| and returns all of the files it contains.
std::vector<std::string> ReadFileContentsImpl(const std::string& dirname) {
  std::vector<std::string> files;
  depthFirstTraverseDirectory(dirname, files);
  return files;
}

// Reads the contents of |filename| as a string into |contents|.
void ReadFileContentsImpl(quiche::QuicheStringPiece filename, std::string* contents) {
#ifdef WIN32
  Envoy::Filesystem::InstanceImplWin32 fs;
#else
  Envoy::Filesystem::InstanceImplPosix fs;
#endif
  *contents = fs.fileReadToEnd(std::string(filename.data(), filename.size()));
}

} // namespace quic
