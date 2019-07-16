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
#ifdef WIN32
    auto file_impl = dynamic_cast<FileImplWin32*>(file);
#else
    auto file_impl = dynamic_cast<FileImplPosix*>(file);
#endif
    RELEASE_ASSERT(file_impl != nullptr, "failed to cast File* to FileImpl*");
    return file_impl->fd_;
  }
#ifdef WIN32
  InstanceImplWin32 file_system_;
#else
  Api::SysCallStringResult canonicalPath(const std::string& path) {
    return file_system_.canonicalPath(path);
  }
  InstanceImplPosix file_system_;
#endif
};

TEST_F(FileSystemImplTest, fileExists) {
  EXPECT_FALSE(file_system_.fileExists("/dev/blahblahblah"));
#ifdef WIN32
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", "x");
  EXPECT_TRUE(file_system_.fileExists(file_path));
  EXPECT_TRUE(file_system_.fileExists("c:/windows"));
#else
  EXPECT_TRUE(file_system_.fileExists("/dev/null"));
  EXPECT_TRUE(file_system_.fileExists("/dev"));
#endif
}

TEST_F(FileSystemImplTest, directoryExists) {
  EXPECT_FALSE(file_system_.directoryExists("/dev/blahblah"));
#ifdef WIN32
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", "x");
  EXPECT_FALSE(file_system_.directoryExists(file_path));
  EXPECT_TRUE(file_system_.directoryExists("c:/windows"));
#else
  EXPECT_FALSE(file_system_.directoryExists("/dev/null"));
  EXPECT_TRUE(file_system_.directoryExists("/dev"));
#endif
}

TEST_F(FileSystemImplTest, fileSize) {
#ifdef WIN32
  EXPECT_EQ(0, file_system_.fileSize("NUL"));
#else
  EXPECT_EQ(0, file_system_.fileSize("/dev/null"));
#endif
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

#ifndef WIN32
TEST_F(FileSystemImplTest, CanonicalPathSuccess) { EXPECT_EQ("/", canonicalPath("//").rc_); }
#endif

#ifndef WIN32
TEST_F(FileSystemImplTest, CanonicalPathFail) {
  const Api::SysCallStringResult result = canonicalPath("/_some_non_existent_file");
  EXPECT_TRUE(result.rc_.empty());
  EXPECT_STREQ("No such file or directory", ::strerror(result.errno_));
}
#endif

TEST_F(FileSystemImplTest, IllegalPath) {
  EXPECT_FALSE(file_system_.illegalPath("/"));
  EXPECT_FALSE(file_system_.illegalPath("//"));
#ifdef WIN32
  EXPECT_FALSE(file_system_.illegalPath("/dev"));
  EXPECT_FALSE(file_system_.illegalPath("/dev/"));
  EXPECT_FALSE(file_system_.illegalPath("/proc"));
  EXPECT_FALSE(file_system_.illegalPath("/proc/"));
  EXPECT_FALSE(file_system_.illegalPath("/sys"));
  EXPECT_FALSE(file_system_.illegalPath("/sys/"));
  EXPECT_FALSE(file_system_.illegalPath("/_some_non_existent_file"));
#else
  EXPECT_TRUE(file_system_.illegalPath("/dev"));
  EXPECT_TRUE(file_system_.illegalPath("/dev/"));
  // Exception to allow opening from file descriptors. See #7258.
  EXPECT_FALSE(file_system_.illegalPath("/dev/fd/0"));
  EXPECT_TRUE(file_system_.illegalPath("/proc"));
  EXPECT_TRUE(file_system_.illegalPath("/proc/"));
  EXPECT_TRUE(file_system_.illegalPath("/sys"));
  EXPECT_TRUE(file_system_.illegalPath("/sys/"));
  EXPECT_TRUE(file_system_.illegalPath("/_some_non_existent_file"));
#endif
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
  const Api::IoCallBoolResult result = file->open();
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenTwice) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  EXPECT_EQ(getFd(file.get()), -1);

  const Api::IoCallBoolResult result1 = file->open();
  const int initial_fd = getFd(file.get());
  EXPECT_TRUE(result1.rc_);
  EXPECT_TRUE(file->isOpen());

  // check that we don't leak a file descriptor
  const Api::IoCallBoolResult result2 = file->open();
  EXPECT_EQ(initial_fd, getFd(file.get()));
  EXPECT_TRUE(result2.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(FileSystemImplTest, OpenBadFilePath) {
  FilePtr file = file_system_.createFile("");
  const Api::IoCallBoolResult result = file->open();
  EXPECT_FALSE(result.rc_);
}

TEST_F(FileSystemImplTest, ExistingFile) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "existing file");

  {
    FilePtr file = file_system_.createFile(file_path);
    const Api::IoCallBoolResult open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::IoCallSizeResult result = file->write(data);
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
    const Api::IoCallBoolResult open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::IoCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.rc_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(new_file_path);
  EXPECT_EQ(" new data", contents);
}

TEST_F(FileSystemImplTest, Close) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  const Api::IoCallBoolResult result1 = file->open();
  EXPECT_TRUE(result1.rc_);
  EXPECT_TRUE(file->isOpen());

  const Api::IoCallBoolResult result2 = file->close();
  EXPECT_TRUE(result2.rc_);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(FileSystemImplTest, WriteAfterClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  FilePtr file = file_system_.createFile(new_file_path);
  const Api::IoCallBoolResult bool_result1 = file->open();
  EXPECT_TRUE(bool_result1.rc_);
  const Api::IoCallBoolResult bool_result2 = file->close();
  EXPECT_TRUE(bool_result2.rc_);
  const Api::IoCallSizeResult size_result = file->write(" new data");
  EXPECT_EQ(-1, size_result.rc_);
  EXPECT_EQ(IoFileError::IoErrorCode::UnknownError, size_result.err_->getErrorCode());
  EXPECT_EQ("Bad file descriptor", size_result.err_->getErrorDetails());
}

} // namespace Filesystem
} // namespace Envoy
