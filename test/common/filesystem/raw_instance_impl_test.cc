#include <chrono>
#include <string>

#include "common/common/assert.h"
#include "common/filesystem/raw_instance_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/test_base.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Filesystem {

class RawInstanceImplTest : public TestBase {
protected:
  int getFd(RawFile* file) {
    auto file_impl = dynamic_cast<RawFileImpl*>(file);
    RELEASE_ASSERT(file_impl != nullptr, "failed to cast RawFile* to RawFileImpl*");
    return file_impl->fd_;
  }

  RawInstanceImpl raw_instance_;
};

TEST_F(RawInstanceImplTest, fileExists) {
  EXPECT_TRUE(raw_instance_.fileExists("/dev/null"));
  EXPECT_FALSE(raw_instance_.fileExists("/dev/blahblahblah"));
}

TEST_F(RawInstanceImplTest, directoryExists) {
  EXPECT_TRUE(raw_instance_.directoryExists("/dev"));
  EXPECT_FALSE(raw_instance_.directoryExists("/dev/null"));
  EXPECT_FALSE(raw_instance_.directoryExists("/dev/blahblah"));
}

TEST_F(RawInstanceImplTest, fileSize) {
  EXPECT_EQ(0, raw_instance_.fileSize("/dev/null"));
  EXPECT_EQ(-1, raw_instance_.fileSize("/dev/blahblahblah"));
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);
  EXPECT_EQ(data.length(), raw_instance_.fileSize(file_path));
}

TEST_F(RawInstanceImplTest, fileReadToEndSuccess) {
  const std::string data = "test string\ntest";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  EXPECT_EQ(data, raw_instance_.fileReadToEnd(file_path));
}

// Files are read into std::string; verify that all bytes (eg non-ascii characters) come back
// unmodified
TEST_F(RawInstanceImplTest, fileReadToEndSuccessBinary) {
  std::string data;
  for (unsigned i = 0; i < 256; i++) {
    data.push_back(i);
  }
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", data);

  const std::string read = raw_instance_.fileReadToEnd(file_path);
  const std::vector<uint8_t> binary_read(read.begin(), read.end());
  EXPECT_EQ(binary_read.size(), 256);
  for (unsigned i = 0; i < 256; i++) {
    EXPECT_EQ(binary_read.at(i), i);
  }
}

TEST_F(RawInstanceImplTest, fileReadToEndDoesNotExist) {
  unlink(TestEnvironment::temporaryPath("envoy_this_not_exist").c_str());
  EXPECT_THROW(raw_instance_.fileReadToEnd(TestEnvironment::temporaryPath("envoy_this_not_exist")),
               EnvoyException);
}

TEST_F(RawInstanceImplTest, fileReadToEndBlacklisted) {
  EXPECT_THROW(raw_instance_.fileReadToEnd("/dev/urandom"), EnvoyException);
  EXPECT_THROW(raw_instance_.fileReadToEnd("/proc/cpuinfo"), EnvoyException);
  EXPECT_THROW(raw_instance_.fileReadToEnd("/sys/block/sda/dev"), EnvoyException);
}

TEST_F(RawInstanceImplTest, CanonicalPathSuccess) {
  EXPECT_EQ("/", raw_instance_.canonicalPath("//").rc_);
}

TEST_F(RawInstanceImplTest, CanonicalPathFail) {
  const Api::SysCallStringResult result = raw_instance_.canonicalPath("/_some_non_existent_file");
  EXPECT_TRUE(result.rc_.empty());
  EXPECT_STREQ("No such file or directory", ::strerror(result.errno_));
}

TEST_F(RawInstanceImplTest, IllegalPath) {
  EXPECT_FALSE(raw_instance_.illegalPath("/"));
  EXPECT_TRUE(raw_instance_.illegalPath("/dev"));
  EXPECT_TRUE(raw_instance_.illegalPath("/dev/"));
  EXPECT_TRUE(raw_instance_.illegalPath("/proc"));
  EXPECT_TRUE(raw_instance_.illegalPath("/proc/"));
  EXPECT_TRUE(raw_instance_.illegalPath("/sys"));
  EXPECT_TRUE(raw_instance_.illegalPath("/sys/"));
  EXPECT_TRUE(raw_instance_.illegalPath("/_some_non_existent_file"));
}

TEST_F(RawInstanceImplTest, ConstructedFileNotOpen) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(RawInstanceImplTest, Open) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  const auto result = file->open();
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(RawInstanceImplTest, OpenTwice) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  EXPECT_EQ(getFd(file.get()), -1);

  auto result = file->open();
  const int initial_fd = getFd(file.get());
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());

  // check that we don't leak a file descriptor
  result = file->open();
  EXPECT_EQ(initial_fd, getFd(file.get()));
  EXPECT_TRUE(result.rc_);
  EXPECT_TRUE(file->isOpen());
}

TEST_F(RawInstanceImplTest, OpenBadFilePath) {
  RawFilePtr file = raw_instance_.createRawFile("");
  const auto result = file->open();
  EXPECT_FALSE(result.rc_);
}

TEST_F(RawInstanceImplTest, ExistingFile) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("test_envoy", "existing file");

  {
    RawFilePtr file = raw_instance_.createRawFile(file_path);
    const auto open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::SysCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.rc_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(file_path);
  EXPECT_EQ("existing file new data", contents);
}

TEST_F(RawInstanceImplTest, NonExistingFile) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  {
    RawFilePtr file = raw_instance_.createRawFile(new_file_path);
    const auto open_result = file->open();
    EXPECT_TRUE(open_result.rc_);
    std::string data(" new data");
    const Api::SysCallSizeResult result = file->write(data);
    EXPECT_EQ(data.length(), result.rc_);
  }

  auto contents = TestEnvironment::readFileToStringForTest(new_file_path);
  EXPECT_EQ(" new data", contents);
}

TEST_F(RawInstanceImplTest, Close) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  const auto result = file->close();
  EXPECT_TRUE(result.rc_);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(RawInstanceImplTest, CloseTwice) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  auto result = file->close();
  EXPECT_TRUE(result.rc_);
  EXPECT_FALSE(file->isOpen());

  // check that closing an already closed file doesn't error
  result = file->close();
  EXPECT_TRUE(result.rc_);
  EXPECT_FALSE(file->isOpen());
}

TEST_F(RawInstanceImplTest, WriteAfterClose) {
  const std::string new_file_path = TestEnvironment::temporaryPath("envoy_this_not_exist");
  ::unlink(new_file_path.c_str());

  RawFilePtr file = raw_instance_.createRawFile(new_file_path);
  auto bool_result = file->open();
  EXPECT_TRUE(bool_result.rc_);
  bool_result = file->close();
  EXPECT_TRUE(bool_result.rc_);
  const Api::SysCallSizeResult result = file->write(" new data");
  EXPECT_EQ(-1, result.rc_);
  EXPECT_EQ(EBADF, result.errno_);
}

} // namespace Filesystem
} // namespace Envoy
