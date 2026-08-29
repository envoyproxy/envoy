#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/syslog/rfc3164_formatter.h"
#include "source/extensions/access_loggers/syslog/rfc5424_formatter.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

const std::string TestAccessLog = R"([2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)";
constexpr int64_t TestTimestampSeconds = 1787149254;

Formatter::FormatterConstSharedPtr makeBodyFormatter(absl::string_view body) {
  return Formatter::FormatterConstSharedPtr(
      std::move(*Formatter::FormatterImpl::create(std::string(body))));
}

TEST(Rfc3164FormatterTest, FormatsCompleteMessageToOutput) {
  Rfc3164Formatter formatter(makeBodyFormatter(TestAccessLog), "<190>", "", "envoy");
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));
  std::string output;

  formatter.formatTo(output, Formatter::Context{}, stream_info);
  EXPECT_EQ(R"(<190>Aug 19 14:20:54 envoy: [2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)",
            output);
}

TEST(Rfc5424FormatterTest, FormatsCompleteMessage) {
  Rfc5424Formatter formatter(makeBodyFormatter(TestAccessLog), "<190>", "hostname", "envoy");
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const SystemTime timestamp = std::chrono::system_clock::from_time_t(TestTimestampSeconds) +
                               std::chrono::microseconds(123456);
  stream_info.ts_.setSystemTime(timestamp);

  EXPECT_THAT(
      formatter.format(Formatter::Context{}, stream_info),
      testing::MatchesRegex(
          R"(<190>1 2026-08-19T14:20:54.123456Z hostname envoy [0-9]+ envoy\.access - \[2026-08-19T14:20:54\.123Z\] "GET / HTTP/1\.1" 200 12 1ms)"));
}

TEST(Rfc5424FormatterTest, HandlesNilHeaderFieldsAndEmptyBody) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));

  Rfc5424Formatter nil_header_formatter(makeBodyFormatter(TestAccessLog), "<190>", "", "");
  EXPECT_THAT(
      nil_header_formatter.format(Formatter::Context{}, stream_info),
      testing::MatchesRegex(
          R"(<190>1 2026-08-19T14:20:54.000000Z - - [0-9]+ envoy\.access - \[2026-08-19T14:20:54\.123Z\] "GET / HTTP/1\.1" 200 12 1ms)"));

  Rfc5424Formatter nil_msg_id_formatter(makeBodyFormatter(TestAccessLog), "<190>", "hostname",
                                        "envoy", "");
  EXPECT_THAT(
      nil_msg_id_formatter.format(Formatter::Context{}, stream_info),
      testing::MatchesRegex(
          R"(<190>1 2026-08-19T14:20:54.000000Z hostname envoy [0-9]+ - - \[2026-08-19T14:20:54\.123Z\] "GET / HTTP/1\.1" 200 12 1ms)"));

  Rfc5424Formatter empty_body_formatter(makeBodyFormatter(""), "<190>", "hostname", "envoy");
  EXPECT_THAT(empty_body_formatter.format(Formatter::Context{}, stream_info),
              testing::MatchesRegex(
                  R"(<190>1 2026-08-19T14:20:54.000000Z hostname envoy [0-9]+ envoy\.access -)"));
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
