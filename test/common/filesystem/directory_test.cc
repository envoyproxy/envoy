#include <fstream>
#include <stack>
#include <string>
#include <unordered_set>

#include "common/filesystem/directory.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

class DirectoryTest : public testing::Test {
public:
  DirectoryTest() : dir_path_(TestEnvironment::temporaryPath("envoy_test")) {
    files_to_remove_.push(dir_path_);
  }

protected:
  void SetUp() override { TestEnvironment::createPath(dir_path_); }

  void TearDown() override {
    while (!files_to_remove_.empty()) {
      const std::string& f = files_to_remove_.top();
      TestEnvironment::removePath(f);
      files_to_remove_.pop();
    }
  }

  void addSubDirs(std::list<std::string> sub_dirs) {
    for (const std::string& dir_name : sub_dirs) {
      const std::string full_path = dir_path_ + "/" + dir_name;
      TestEnvironment::createPath(full_path);
      files_to_remove_.push(full_path);
    }
  }

  void addFiles(std::list<std::string> files) {
    for (const std::string& file_name : files) {
      const std::string full_path = dir_path_ + "/" + file_name;
      { const std::ofstream file(full_path); }
      files_to_remove_.push(full_path);
    }
  }

  void addSymlinks(std::list<std::pair<std::string, std::string>> symlinks) {
    for (const auto& link : symlinks) {
      const std::string target_path = dir_path_ + "/" + link.first;
      const std::string link_path = dir_path_ + "/" + link.second;
      TestEnvironment::createSymlink(target_path, link_path);
      files_to_remove_.push(link_path);
    }
  }

  const std::string dir_path_;
  std::stack<std::string> files_to_remove_;
};

struct EntryHash {
  std::size_t operator()(DirectoryEntry const& e) const noexcept {
    return std::hash<std::string>{}(e.name_);
  }
};

using EntrySet = std::unordered_set<DirectoryEntry, EntryHash>;

EntrySet getDirectoryContents(const std::string& dir_path, bool recursive) {
  Directory directory(dir_path);
  EntrySet ret;
  for (const DirectoryEntry& entry : directory) {
    ret.insert(entry);
    if (entry.type_ == FileType::Directory && entry.name_ != "." && entry.name_ != ".." &&
        recursive) {
      std::string subdir_name = entry.name_;
      EntrySet subdir = getDirectoryContents(dir_path + "/" + subdir_name, recursive);
      for (const DirectoryEntry& entry : subdir) {
        ret.insert({subdir_name + "/" + entry.name_, entry.type_});
      }
    }
  }
  return ret;
}

// Test that we can list a file in a directory
TEST_F(DirectoryTest, DirectoryWithOneFile) {
  addFiles({"file"});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that we can list a sub directory in a directory
TEST_F(DirectoryTest, DirectoryWithOneDirectory) {
  addSubDirs({"sub_dir"});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that we do not recurse into directories when listing files
TEST_F(DirectoryTest, DirectoryWithFileInSubDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"sub_dir/sub_file"});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that when recursively creating DirectoryIterators, they do not interfere with each other
TEST_F(DirectoryTest, RecursionIntoSubDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"file", "sub_dir/sub_file"});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
      {"sub_dir", FileType::Directory},
      {"sub_dir/sub_file", FileType::Regular},
      {"sub_dir/.", FileType::Directory},
      {"sub_dir/..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, true));
}

// Test that we can list a file and a sub directory in a directory
TEST_F(DirectoryTest, DirectoryWithFileAndDirectory) {
  addSubDirs({"sub_dir"});
  addFiles({"file"});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
      {"file", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that a symlink to a file has type FileType::Regular
TEST_F(DirectoryTest, DirectoryWithSymlinkToFile) {
  addFiles({"file"});
  addSymlinks({{"file", "link"}});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"file", FileType::Regular},
      {"link", FileType::Regular},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that a symlink to a directory has type FileType::Directory
TEST_F(DirectoryTest, DirectoryWithSymlinkToDirectory) {
  addSubDirs({"sub_dir"});
  addSymlinks({{"sub_dir", "link_dir"}});

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
      {"sub_dir", FileType::Directory},
      {"link_dir", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that a broken symlink can be listed
TEST_F(DirectoryTest, DirectoryWithBrokenSymlink) {
  addSubDirs({"sub_dir"});
  addSymlinks({{"sub_dir", "link_dir"}});
  TestEnvironment::removePath(dir_path_ + "/sub_dir");

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
#ifndef WIN32
      // On Linux, a broken directory link is simply a symlink to be rm'ed
      {"link_dir", FileType::Regular},
#else
      // On Windows, a broken directory link remains a directory link to be rmdir'ed
      {"link_dir", FileType::Directory},
#endif
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that we can list an empty directory
TEST_F(DirectoryTest, DirectoryWithEmptyDirectory) {
  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path_, false));
}

// Test that the constructor throws an exception when given a non-existing path
TEST(DirectoryIteratorImpl, NonExistingDir) {
  const std::string dir_path("some/non/existing/dir");

#ifdef WIN32
  EXPECT_THROW_WITH_MESSAGE(
      DirectoryIteratorImpl dir_iterator(dir_path), EnvoyException,
      fmt::format("unable to open directory {}: {}", dir_path, ERROR_PATH_NOT_FOUND));
#else
  EXPECT_THROW_WITH_MESSAGE(
      DirectoryIteratorImpl dir_iterator(dir_path), EnvoyException,
      fmt::format("unable to open directory {}: No such file or directory", dir_path));
#endif
}

// Test that we correctly handle trailing path separators
TEST(Directory, DirectoryHasTrailingPathSeparator) {
#ifdef WIN32
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "\\");
#else
  const std::string dir_path(TestEnvironment::temporaryPath("envoy_test") + "/");
#endif
  TestEnvironment::createPath(dir_path);

  const EntrySet expected = {
      {".", FileType::Directory},
      {"..", FileType::Directory},
  };
  EXPECT_EQ(expected, getDirectoryContents(dir_path, false));
  TestEnvironment::removePath(dir_path);
}

} // namespace Filesystem
} // namespace Envoy
