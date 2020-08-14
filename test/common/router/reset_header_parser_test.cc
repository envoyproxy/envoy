#include "envoy/config/route/v3/route_components.pb.h"
#include "envoy/http/protocol.h"
#include "envoy/json/json_object.h"

#include "common/json/json_loader.h"
#include "common/router/reset_header_parser.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

envoy::config::route::v3::RetryPolicy::ResetHeader
parseResetHeaderParserFromYaml(const std::string& yaml) {
  envoy::config::route::v3::RetryPolicy::ResetHeader reset_header;
  TestUtility::loadFromYaml(yaml, reset_header);
  return reset_header;
}

TEST(ResetHeaderParserConstructorTest, FormatUnset) {
  const std::string yaml = R"EOF(
name: retry-after
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  EXPECT_EQ("retry-after", reset_header_parser.name().get());
  EXPECT_EQ(ResetHeaderFormat::Seconds, reset_header_parser.format());
}

TEST(ResetHeaderParserConstructorTest, FormatSeconds) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  EXPECT_EQ("retry-after", reset_header_parser.name().get());
  EXPECT_EQ(ResetHeaderFormat::Seconds, reset_header_parser.format());
}

TEST(ResetHeaderParserConstructorTest, FormatUnixTimestamp) {
  const std::string yaml = R"EOF(
name: retry-after
format: UNIX_TIMESTAMP
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  EXPECT_EQ("retry-after", reset_header_parser.name().get());
  EXPECT_EQ(ResetHeaderFormat::UnixTimestamp, reset_header_parser.format());
}

class ResetHeaderParserParseIntervalTest : public testing::Test {
public:
  ResetHeaderParserParseIntervalTest() {
    const time_t known_date_time = 1000000000;
    test_time_.setSystemTime(std::chrono::system_clock::from_time_t(known_date_time));
  }

  Event::SimulatedTimeSystem test_time_;
};

TEST_F(ResetHeaderParserParseIntervalTest, NoHeaderMatches) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};

  EXPECT_EQ(absl::nullopt,
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesButUnsupportedFormatDate) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{
      {"retry-after", "Fri, 17 Jul 2020 11:59:51 GMT"}};

  EXPECT_EQ(absl::nullopt,
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesButUnsupportedFormatFloat) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "2.5"}};

  EXPECT_EQ(absl::nullopt,
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesSupportedFormatSeconds) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "5"}};

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(5000),
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesSupportedFormatSecondsCaseInsensitive) {
  const std::string yaml = R"EOF(
name: retry-after
format: SECONDS
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"Retry-After", "5"}};

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(5000),
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesButUnsupportedFormatTimestampFloat) {
  const std::string yaml = R"EOF(
name: retry-after
format: UNIX_TIMESTAMP
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "1595320702.1234"}};

  EXPECT_EQ(absl::nullopt,
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesSupportedFormatTimestampButInThePast) {
  const std::string yaml = R"EOF(
name: retry-after
format: UNIX_TIMESTAMP
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "999999999"}};

  EXPECT_EQ(absl::nullopt,
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesSupportedFormatTimestampEmptyInterval) {
  const std::string yaml = R"EOF(
name: retry-after
format: UNIX_TIMESTAMP
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "1000000000"}};

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(0),
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

TEST_F(ResetHeaderParserParseIntervalTest, HeaderMatchesSupportedFormatTimestampNonEmptyInterval) {
  const std::string yaml = R"EOF(
name: retry-after
format: UNIX_TIMESTAMP
  )EOF";

  ResetHeaderParserImpl reset_header_parser =
      ResetHeaderParserImpl(parseResetHeaderParserFromYaml(yaml));

  Http::TestResponseHeaderMapImpl response_headers{{"retry-after", "1000000007"}};

  EXPECT_EQ(absl::optional<std::chrono::milliseconds>(7000),
            reset_header_parser.parseInterval(test_time_.timeSystem(), response_headers));
}

} // namespace
} // namespace Router
} // namespace Envoy
