#include "envoy/extensions/access_loggers/stream/v3/stream.pb.h"

#include "test/extensions/access_loggers/stream/stream_test_base.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace File {
namespace {

class StderrAccessLogTest
    : public StreamAccessLogTest<envoy::extensions::access_loggers::stream::v3::StderrAccessLog,
                                 Filesystem::DestinationType::Stderr> {
public:
  StderrAccessLogTest() = default;
};

TEST_F(StderrAccessLogTest, EmptyFormat) {
  runTest(
      "{}",
      "[2018-12-18T01:50:34.000Z] \"GET /bar/foo -\" 200 - 0 0 - - \"-\" \"-\" \"-\" \"-\" \"-\"\n",
      false);
}

TEST_F(StderrAccessLogTest, LogFormatText) {
  runTest(
      R"(
  log_format:
    text_format_source:
      inline_string: "plain_text - %REQ(:path)% - %RESPONSE_CODE%"
)",
      "plain_text - /bar/foo - 200", false);
}

TEST_F(StderrAccessLogTest, LogFormatJson) {
  runTest(
      R"(
  log_format:
    json_format:
      text: "plain text"
      path: "%REQ(:path)%"
      code: "%RESPONSE_CODE%"
)",
      R"({
    "text": "plain text",
    "path": "/bar/foo",
    "code": 200
})",
      true);
}

TEST_F(StreamAccessLogExtensionConfigYamlTest, Stderr) {
  const std::string yaml = R"EOF(
name: accesslog
typed_config:
  "@type": type.googleapis.com/envoy.extensions.access_loggers.stream.v3.StderrAccessLog
  )EOF";
  runTest(yaml, Filesystem::DestinationType::Stderr);
}

} // namespace
} // namespace File
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
