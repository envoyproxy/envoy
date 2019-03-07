#include <chrono>
#include <string>

#include "common/common/assert.h"
#include "common/filesystem/filesystem_impl.h"

#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

class FileSystemImplTest : public testing::Test {
protected:
  int getFd(File* file) {
    auto file_impl = dynamic_cast<FileImpl*>(file);
    RELEASE_ASSERT(file_impl != nullptr, "failed to cast File* to FileImpl*");
    return file_impl->fd_;
  }

  InstanceImpl file_system_;
};

TEST_F(FileSystemImplTest, fileExists) {
  EXPECT_TRUE(file_system_.fileExists("/dev/null"));
  EXPECT_FALSE(file_system_.fileExists("/dev/blahblahblah"));
}

TEST_F(FileSystemImplTest, directoryExists) {
  EXPECT_TRUE(file_system_.directoryExists("/dev"));
  EXPECT_FALSE(file_system_.directoryExists("/dev/null"));
  EXPECT_FALSE(file_system_.directoryExists("/dev/blahblah"));
}

TEST_F(FileSystemImplTest, fileSize) {
  EXPECT_EQ(0, file_system_.fileSize("/dev/null"));
  EXPECT_EQ(-1, file_system_.fileSize("/dev/blahblahblah"));
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);
  EXPECT_EQ(data.length(), file_system_.fileSize(file_path));
}

TEST_F(FileSystemImplTest, fileReadToEndSuccess) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  EXPECT_EQ(data, file_system_.fileReadToEnd(file_path));
}

// Files are read into std::string; verify that all bytes (eg non-ascii characters) come back
// unmodified
TEST_F(FileSystemImplTest, fileReadToEndSuccessBinary) {
  std::string data;
  for (unsigned i = 0; i < 256; i++) {
    data.push_back(i);
  }
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  const std::string read = file_system_.fileReadToEnd(file_path);
  const std::vector<uint8_t> binary_read(read.begin(), read.end());
  EXPECT_EQ(binary_read.size(), 256);
  for (unsigned i = 0; i < 256; i++) {
    EXPECT_EQ(binary_read.at(i), i);
  }
}

TEST_F(FileSystemImplTest, fileReadToEndDoesNotExist) {
  unlink(TestEnvironment::temporaryPath("envoy_this_not_exist").c_str());
  EXPECT_THROW(file_system_.fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist")),
               EnvoyException);
}

TEST_F(FileSystemImplTest, fileReadToEndBlacklisted) {
  EXPECT_THROW(file_system_.fileReadToEnd("/dev/urandom"), EnvoyException);
  EXPECT_THROW(file_system_.fileReadToEnd("/proc/cpuinfo"), EnvoyException);
  EXPECT_THROW(file_system_.fileReadToEnd("/sys/block/sda/dev"), EnvoyException);
}

TEST_F(FileSystemImplTest, CanonicalPathSuccess) {
  EXPECT_EQ("/", file_system_.canonicalPath("//").rc_);
}

TEST_F(FileSystemImplTest, CanonicalPathFail) {
  const Api::SysCallStringResult result = file_system_.canonicalPath("/_some_non_existent_file");
  EXPECT_TRUE(result.rc_.empty());
  EXPECT_STREQ("No such file or directory", ::strerror(result.errno_));
}

TEST_F(FileSystemImplTest, IllegalPath) {
  EXPECT_FALSE(file_system_.illegalPath("/"));
  EXPECT_TRUE(file_system_.illegalPath("/dev"));
  EXPECT_TRUE(file_system_.illegalPath("/dev/"));
  EXPECT_TRUE(file_system_.illegalPath("/proc"));
  EXPECT_TRUE(file_system_.illegalPath("/proc/"));
  EXPECT_TRUE(file_system_.illegalPath("/sys"));
  EXPECT_TRUE(file_system_.illegalPath("/sys/"));
  EXPECT_TRUE(file_system_.illegalPath("/_some_non_existent_file"));
}

TEST_F(FileSystemImplTest, ConstructedFileNotOpen) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(FileSystemImplTest, Open) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  const Api::SysCallBoolResult result = file->open();
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenTwice) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  EXPECT_EQ(getFd(file.get()), -1);

  Api::SysCallBoolResult result = file->open();
  const int initial_fd = getFd(file.get());
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());

  // check that we don't leak a file descriptor
  result = file->open();
  EXPECT_EQ(initial_fd, getFd(file.get()));
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenBadFilePath) {
  FilePtr file = file_system_.createFile("");
  const Api::SysCallBoolResult result = file->open();
  EXPECT_FALSE(result.rc_);
}

TEST_F(FileSystemImplTest, ExistingFile) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "existing file");

  {
    FilePtr file = file_system_.createFile(file_path);
    const Api::SysCallBoolResult open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::SysCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.rc_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ("existing file new data", contents);
}

TEST_F(FileSystemImplTest, NonExistingFile) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  {
    FilePtr file = file_system_.createFile(new_file_path);
    const Api::SysCallBoolResult open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::SysCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.rc_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(new_file_path);
  EXPECT_EQ(" new data", contents);
}

TEST_F(FileSystemImplTest, Close) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  Api::SysCallBoolResult result = file->open();
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());

  result = file->close();
  EXPECT_TRUE(result.rc_);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(FileSystemImplTest, WriteAfterClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  Api::SysCallBoolResult bool_result = file->open();
  EXPECT_TRUE(bool_result.rc_);
  bool_result = file->close();
  EXPECT_TRUE(bool_result.rc_);
  const Api::SysCallSizeResult result = file->write(" new data");
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(EBADF, result.errno_);
}

} // namespace Filesystem
} // namespace Envoy
