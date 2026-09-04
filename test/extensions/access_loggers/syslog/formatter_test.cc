#include <string>

#include "source/common/formatter/substitution_formatter.h"
#include "source/extensions/access_loggers/syslog/formatter.h"

#include "test/mocks/stream_info/mocks.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

using testing::MatchesRegex;

const std::string TestAccessLog = R"([2026-08-19T14:20:54.123Z] "GET / HTTP/1.1" 200 12 1ms)";
constexpr int64_t TestTimestampSeconds = 1787149254;
const SystemTime TestTimestamp = std::chrono::system_clock::from_time_t(TestTimestampSeconds);

Formatter::FormatterConstSharedPtr makeBodyFormatter(absl::string_view body) {
  return Formatter::FormatterConstSharedPtr(
      std::move(*Formatter::FormatterImpl::create(std::string(body))));
}

SyslogAccessLogConfig baseConfig() {
  SyslogAccessLogConfig config;
  config.set_omit_hostname(true);
  return config;
}

Rfc3164HeaderFormatter makeRfc3164HeaderFormatter(const SyslogAccessLogConfig& config) {
  return {config.facility(), config.severity(), config.omit_hostname(), config.tag()};
}

Rfc5424HeaderFormatter makeRfc5424HeaderFormatter(const SyslogAccessLogConfig& config) {
  return {config.facility(), config.severity(), config.omit_hostname(), config.tag(),
          config.msg_id()};
}

TEST(Rfc3164HeaderFormatterTest, FormatsConfiguredFieldsAndTimestamp) {
  const Rfc3164HeaderFormatter default_header = makeRfc3164HeaderFormatter(baseConfig());
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: ", default_header.format(TestTimestamp));

  auto config = baseConfig();
  config.set_facility(SyslogAccessLogConfig::FACILITY_USER);
  config.set_severity(SyslogAccessLogConfig::DEBUG);
  config.set_tag("edge");
  const Rfc3164HeaderFormatter configured_header = makeRfc3164HeaderFormatter(config);
  EXPECT_EQ("<15>Aug 19 14:20:54 edge: ", configured_header.format(TestTimestamp));
}

TEST(Rfc5424HeaderFormatterTest, FormatsConfiguredFieldsAndTimestamp) {
  const Rfc5424HeaderFormatter default_header = makeRfc5424HeaderFormatter(baseConfig());
  EXPECT_THAT(default_header.format(TestTimestamp),
              MatchesRegex("<190>1 2026-08-19T14:20:54\\.000000Z - envoy [0-9]+ "
                           "envoy\\.access -"));

  auto config = baseConfig();
  config.set_facility(SyslogAccessLogConfig::FACILITY_USER);
  config.set_severity(SyslogAccessLogConfig::DEBUG);
  config.set_tag("edge");
  config.set_msg_id("envoy.audit");
  const Rfc5424HeaderFormatter configured_header = makeRfc5424HeaderFormatter(config);
  EXPECT_THAT(configured_header.format(TestTimestamp),
              MatchesRegex("<15>1 2026-08-19T14:20:54\\.000000Z - edge [0-9]+ "
                           "envoy\\.audit -"));
}

TEST(Rfc3164FormatterTest, CombinesHeaderAndBody) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.ts_.setSystemTime(TestTimestamp);

  Rfc3164Formatter formatter(makeBodyFormatter(TestAccessLog),
                             makeRfc3164HeaderFormatter(baseConfig()));
  EXPECT_EQ("<190>Aug 19 14:20:54 envoy: " + TestAccessLog,
            formatter.format(Formatter::Context{}, stream_info));
}

TEST(Rfc5424FormatterTest, CombinesHeaderAndOptionalBody) {
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
  stream_info.ts_.setSystemTime(TestTimestamp);
  const Rfc5424HeaderFormatter header = makeRfc5424HeaderFormatter(baseConfig());

  Rfc5424Formatter formatter(makeBodyFormatter(TestAccessLog), header);
  EXPECT_EQ(header.format(TestTimestamp) + " " + TestAccessLog,
            formatter.format(Formatter::Context{}, stream_info));

  Rfc5424Formatter empty_body_formatter(makeBodyFormatter(""), header);
  EXPECT_EQ(header.format(TestTimestamp),
            empty_body_formatter.format(Formatter::Context{}, stream_info));
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
