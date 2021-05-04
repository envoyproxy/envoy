#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "library/common/common/lambda_logger_delegate.h"
#include "library/common/data/utility.h"

using testing::_;
using testing::HasSubstr;

namespace Envoy {
namespace Logger {

TEST(LambdaDelegate, LogCb) {
  std::string expected_msg = "Hello LambdaDelegate";
  std::string actual_msg;

  LambdaDelegate delegate =
      LambdaDelegate({[](envoy_data data, const void* context) -> void {
                        auto* actual_msg = static_cast<std::string*>(const_cast<void*>(context));
                        *actual_msg = Data::Utility::copyToString(data);
                        data.release(data.context);
                      },
                      [](const void*) -> void {}, &actual_msg},
                     Registry::getSink());

  ENVOY_LOG_MISC(error, expected_msg);
  EXPECT_THAT(actual_msg, HasSubstr(expected_msg));
}

TEST(LambdaDelegate, ReleaseCb) {
  bool released = false;
  LambdaDelegatePtr delegate = LambdaDelegatePtr(
      new LambdaDelegate({[](envoy_data data, const void*) -> void { data.release(data.context); },
                          [](const void* context) -> void {
                            bool* released = static_cast<bool*>(const_cast<void*>(context));
                            *released = true;
                          },
                          &released},
                         Registry::getSink()));

  delegate.reset(nullptr);
  EXPECT_TRUE(released);
}

} // namespace Logger
} // namespace Envoy
