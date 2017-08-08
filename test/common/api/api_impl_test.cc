#include <chrono>
#include <fstream>
#include <string>

#include "common/api/api_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Api {

TEST(ApiImplTest, readFileToEnd) {
  Impl api(std::chrono::milliseconds(10000));

  const std::string file_path = TestEnvironment::temporaryPath("test_api_envoy");
  unlink(file_path.c_str());

  std::ofstream file(file_path);
  const std::string data = "test read To End\nWith new lines.";
  file << data;
  file.close();

  EXPECT_EQ(data, api.fileReadToEnd(file_path));
}

TEST(ApiImplTest, fileExists) {
  Impl api(std::chrono::milliseconds(10000));

  EXPECT_TRUE(api.fileExists("/dev/null"));
  EXPECT_FALSE(api.fileExists("/dev/blahblahblah"));
}

} // namespace Api
} // namespace Envoy
