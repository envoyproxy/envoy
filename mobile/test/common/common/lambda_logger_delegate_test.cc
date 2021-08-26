#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/api/external.h"
#include "library/common/common/lambda_logger_delegate.h"
#include "library/common/data/utility.h"

using testing::_;
using testing::HasSubstr;

namespace Envoy {
namespace Logger {

class LambdaDelegateTest : public testing::Test {
public:
  static envoy_event_tracker tracker;

  static void SetUpTestSuite() {
    Api::External::registerApi(envoy_event_tracker_api_name, &tracker);
  }
};

envoy_event_tracker LambdaDelegateTest::tracker{};

TEST_F(LambdaDelegateTest, LogCb) {
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  LambdaDelegate delegate({[](envoy_data data, const void* context) -> void {
                             auto* actual_msg =
                                 static_cast<std::string*>(const_cast<void*>(context));
                             *actual_msg = Data::Utility::copyToString(data);
                             release_envoy_data(data);
                           },
                           [](const void*) -> void {}, &actual_msg},
                          Registry::getSink());

  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));
}

TEST_F(LambdaDelegateTest, ReleaseCb) {
  bool released = false;

  {
    LambdaDelegate({[](envoy_data data, const void*) -> void { release_envoy_data(data); },
                    [](const void* context) -> void {
                      bool* released = static_cast<bool*>(const_cast<void*>(context));
                      *released = true;
                    },
                    &released},
                   Registry::getSink());
  }

  EXPECT_TRUE(released);
}

} // namespace Logger
} // namespace Envoy
