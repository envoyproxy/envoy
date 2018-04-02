#include "common/http/header_map_impl.h"
#include "common/tracing/dynamic_opentracing_driver_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"
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

  void setupValidDriver() { setup(library_path_, tracer_config_); }

  const std::string library_path_ =
      TestEnvironment::runfilesDirectory() +
      "/external/io_opentracing_cpp/mocktracer/libmocktracer_plugin.so";
  const std::string spans_file_ = TestEnvironment::temporaryDirectory() + "/spans.json";
  const std::string tracer_config_ = fmt::sprintf(R"EOF(
      {
        "output_file": "%s"
      }
    )EOF",
                                                  spans_file_);
  std::unique_ptr<DynamicOpenTracingDriver> driver_;
  Stats::IsolatedStoreImpl stats_;

  const std::string operation_name_{"test"};
  Http::TestHeaderMapImpl request_headers_{
      {":path", "/"}, {":method", "GET"}, {"x-request-id", "foo"}};
  SystemTime start_time_;
  NiceMock<Tracing::MockConfig> config_;
};

TEST_F(DynamicOpenTracingDriverTest, formatErrorMessage) {
  const std::error_code error_code = std::make_error_code(std::errc::permission_denied);
  EXPECT_EQ(error_code.message(), DynamicOpenTracingDriver::formatErrorMessage(error_code, ""));
  EXPECT_EQ(error_code.message() + ": abc",
            DynamicOpenTracingDriver::formatErrorMessage(error_code, "abc"));
}

TEST_F(DynamicOpenTracingDriverTest, InitializeDriver) {
  {
    std::string invalid_library = "abc123";
    std::string invalid_config = R"EOF(
      {"fake" : "fake"}
    )EOF";

    EXPECT_THROW(setup(invalid_library, invalid_config), EnvoyException);
  }

  {
    std::string empty_config = "{}";

    EXPECT_THROW(setup(library_path_, empty_config), EnvoyException);
  }
}

TEST_F(DynamicOpenTracingDriverTest, FlushSpans) {
  setupValidDriver();

  SpanPtr first_span = driver_->startSpan(config_, request_headers_, operation_name_, start_time_,
                                          {Reason::Sampling, true});
  first_span->finishSpan();
  driver_->tracer().Close();

  const Json::ObjectSharedPtr spans_json =
      TestEnvironment::jsonLoadFromString(TestEnvironment::readFileToStringForTest(spans_file_));
  EXPECT_NE(spans_json, nullptr);
  EXPECT_EQ(spans_json->asObjectArray().size(), 1);
}

} // namespace Tracing
} // namespace Envoy
