#include <chrono>
#include <string>

#include "common/api/api_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Api {

TEST(ApiImplTest, readFileToEnd) {
  Impl api(std::chrono::milliseconds(1000), Thread::threadFactoryForTest());

  const std::string data = "test read To End\nWith new lines.";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_api_envoy", data);

  EXPECT_EQ(data, api.fileReadToEnd(file_path));
}

TEST(ApiImplTest, fileExists) {
  Impl api(std::chrono::milliseconds(1000), Thread::threadFactoryForTest());

  EXPECT_TRUE(api.fileExists("/dev/null"));
  EXPECT_FALSE(api.fileExists("/dev/blahblahblah"));
}

} // namespace Api
} // namespace Envoy
