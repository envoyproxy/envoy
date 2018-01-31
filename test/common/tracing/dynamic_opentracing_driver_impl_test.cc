#include "common/tracing/dynamic_opentracing_driver_impl.h"

#include "test/mocks/stats/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Test;

namespace Envoy {
namespace Tracing {

class DynamicOpenTracingDriverTest : public Test {
public:
  void setup(const std::string& library, const std::string& tracer_config) {
    driver_.reset(new DynamicOpenTracingDriver{stats_, library, tracer_config});
  }

  std::unique_ptr<DynamicOpenTracingDriver> driver_;
  Stats::IsolatedStoreImpl stats_;
};

TEST_F(DynamicOpenTracingDriverTest, InitializeDriver) {
  {
    std::string invalid_library = "abc123";
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";

    EXPECT_THROW(setup(invalid_library, invalid_config), EnvoyException);
  }
}

} // namespace Tracing
} // namespace Envoy
