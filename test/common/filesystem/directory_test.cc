#include <fstream>
#include <string>
#include <unordered_set>

#include "common/filesystem/directory.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

// we are using this class to clean up all the files we create,
// as it looks like some versions of libstdc++ have a bug in
// std::experimental::filesystem::remove_all where it fails with nested directories:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=71313

class ScopedPathRemover {
public:
  ScopedPathRemover(std::list<std::string> paths) : paths_(paths) {}

  ~ScopedPathRemover() {
    for (const std::string& path : paths_) {
      TestEnvironment::removePath(path);
    }
  }

private:
  std::list<std::string> paths_;
};

struct EntryHash {
  std::size_t operator()(DirectoryEntry const& e) const noexcept {
    return std::hash<std::string>{}(e.name_);
  }
};

typedef std::unordered_set<DirectoryEntry, EntryHash> EntrySet;

EntrySet getDirectoryContents(const std::string& dir_path, bool recursive) {
  Directory directory(dir_path);
  EntrySet ret;
  for (const DirectoryEntry entry : directory) {
    ret.insert(entry);
    if (entry.type_ == FileType::Directory && entry.name_ != "." && entry.name_ != ".." &&
        recursive) {
      std::string subdir_name = entry.name_;
      EntrySet subdir = getDirectoryContents(dir_path + "/" + subdir_name, recursive);
      for (const DirectoryEntry entry : subdir) {
        ret.insert({subdir_name + "/" + entry.name_, entry.type_});
      }
    }
  }
  return ret;
}

TEST(Directory, DirectoryWithOneFile) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  ScopedPathRemover s({file_path, dir_path});

  TestUtility::createDirectory(dir_path);
  { std::ofstream file(file_path); }

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, DirectoryWithOneDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, DirectoryWithFileInSubDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  const std::string child_file_path(child_dir_path + "/" + "file");
  ScopedPathRemover s({child_file_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  { std::ofstream file(child_file_path); }

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, RecursionIntoSubDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  const std::string child_file_path(child_dir_path + "/" + "sub_file");
  ScopedPathRemover s({child_file_path, child_dir_path, file_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  { std::ofstream file(file_path); }
  { std::ofstream file(child_file_path); }

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
      {"sub_dir", FileType::Directory},
      {"sub_dir/sub_file", FileType::Regular},
      {"sub_dir/.", FileType::Directory},
      {"sub_dir/..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, true));
}

TEST(Directory, DirectoryWithFileAndDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({file_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  { std::ofstream file(file_path); }

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
      {"file", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, DirectoryWithSymlinkToFile) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string file_path(dir_path + "/" + "file");
  const std::string link_path(dir_path + "/" + "link");
  ScopedPathRemover s({link_path, file_path, dir_path});
  TestUtility::createDirectory(dir_path);
  { std::ofstream file(file_path); }
  TestUtility::createSymlink(file_path, link_path);

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
      {"link", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, DirectoryWithSymlinkToDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  const std::string link_dir_path(dir_path + "/" + "link_dir");
  const std::string child_dir_path(dir_path + "/" + "sub_dir");
  ScopedPathRemover s({link_dir_path, child_dir_path, dir_path});
  TestUtility::createDirectory(dir_path);
  TestUtility::createDirectory(child_dir_path);
  TestUtility::createSymlink(child_dir_path, link_dir_path);

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
      {"link_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(Directory, DirectoryWithEmptyDirectory) {
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test"));
  ScopedPathRemover s({dir_path});
  TestUtility::createDirectory(dir_path);

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

TEST(DirectoryIteratorImpl, NonExistingDir) {
  const std::string dir_path("some/non/existing/dir");

#if !defined(WIN32)
  EXPECT_THROW_WITH_MESSAGE(
      DirectoryIteratorImpl dir_iterator(dir_path), EnvoyException,
      fmt::format("unable to open directory {}: No such file or directory", dir_path));
#else
  EXPECT_THROW_WITH_MESSAGE(
      DirectoryIteratorImpl dir_iterator(dir_path), EnvoyException,
      fmt::format("unable to open directory {}: {}", dir_path, ERROR_PATH_NOT_FOUND));
#endif
}

TEST(Directory, DirectoryHasTrailingPathSeparator) {
#if !defined(WIN32)
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "/");
#else
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "\\");
#endif
  ScopedPathRemover s({dir_path});
  TestUtility::createDirectory(dir_path);

  EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
}

} // namespace Filesystem
} // namespace Envoy