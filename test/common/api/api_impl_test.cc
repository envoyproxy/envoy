#include <chrono>
#include <string>

#include "common/api/api_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Api {

class ApiImplTest : public testing::Test {
protected:
  ApiImplTest() : api_(createApiForTest(store_)) {}

  Stats::IsolatedStoreImpl store_;
  ApiPtr api_;
};

TEST_F(ApiImplTest, readFileToEnd) {
  const std::string data = "test read To End\nWith new lines.";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_api_envoy", data);

  EXPECT_EQ(data, api_->fileReadToEnd(file_path));
}

TEST_F(ApiImplTest, fileExists) {
  EXPECT_TRUE(api_->fileExists("/dev/null"));
  EXPECT_FALSE(api_->fileExists("/dev/blahblahblah"));
}

} // namespace Api
} // namespace Envoy
