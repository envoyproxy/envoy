#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/access_loggers/syslog/syslog_access_log_impl.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {
namespace {

constexpr int64_t TestTimestampSeconds = 1787149254;

uint64_t counterValue(Stats::IsolatedStoreImpl& store, absl::string_view name) {
  const auto counter =
      TestUtility::findCounter(store, absl::StrCat("access_logs.syslog.test.", name));
  return counter != nullptr ? counter->value() : 0;
}

class TestBodyFormatter : public Formatter::Formatter {
public:
  explicit TestBodyFormatter(std::string body) : body_(std::move(body)) {}

  std::string format(const Envoy::Formatter::Context&,
                     const StreamInfo::StreamInfo&) const override {
    return body_;
  }

  void formatTo(std::string& output, const Envoy::Formatter::Context&,
                const StreamInfo::StreamInfo&) const override {
    output.append(body_);
  }

private:
  const std::string body_;
};

class FakeSender : public Sender {
public:
  void send(absl::string_view message) override {
    messages_.emplace_back(message.data(), message.size());
  }

  std::vector<std::string> messages_;
};

SenderPtr makeFakeSender(FakeSender*& sender_ptr) {
  auto sender = std::make_unique<FakeSender>();
  sender_ptr = sender.get();
  return sender;
}

struct LoggerTestContext {
  LoggerTestContext(const SyslogAccessLogConfig& config, std::string body)
      : stats(*store.rootScope(), "test"), sender_ptr(nullptr),
        logger(config, std::make_shared<TestBodyFormatter>(std::move(body)),
               makeFakeSender(sender_ptr), stats) {
    stream_info.ts_.setSystemTime(std::chrono::system_clock::from_time_t(TestTimestampSeconds));
  }

  void log() { logger.log(Formatter::Context{}, stream_info); }

  Stats::IsolatedStoreImpl store;
  SyslogAccessLogStats stats;
  FakeSender* sender_ptr;
  SyslogAccessLoggerImpl logger;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info;
};

SyslogAccessLogConfig baseConfig() {
  SyslogAccessLogConfig config;
  config.set_no_hostname(true);
  return config;
}

TEST(SyslogAccessLoggerImplTest, FormatsRfc3164AndPreservesEmbeddedNull) {
  const std::string body("a\0b", 3);
  LoggerTestContext context(baseConfig(), body);

  context.log();

  ASSERT_EQ(1, context.sender_ptr->messages_.size());
  std::string expected = "<190>Aug 19 14:20:54 envoy: ";
  expected.append(body);
  EXPECT_EQ(expected, context.sender_ptr->messages_[0]);
  EXPECT_EQ(1, counterValue(context.store, "messages.state.full"));
  EXPECT_EQ(0, counterValue(context.store, "messages.state.truncated"));
}

TEST(SyslogAccessLoggerImplTest, FormatsRfc5424) {
  auto config = baseConfig();
  config.set_syslog_format(SyslogAccessLogConfig::RFC5424);
  config.set_msg_id("envoy.audit");
  LoggerTestContext context(config, "payload");

  context.log();

  ASSERT_EQ(1, context.sender_ptr->messages_.size());
  EXPECT_THAT(context.sender_ptr->messages_[0],
              testing::MatchesRegex("<190>1 2026-08-19T14:20:54\\.000000Z - envoy [0-9]+ "
                                    "envoy\\.audit - payload"));
}

TEST(SyslogAccessLoggerImplTest, EnforcesMaxMessageSize) {
  const std::string header = "<190>Aug 19 14:20:54 envoy: ";
  struct TestCase {
    absl::string_view name;
    uint32_t configured_limit;
    size_t original_size;
    size_t expected_size;
  };
  const std::vector<TestCase> cases = {
      {"default under", 0, 1451, 1451},   {"default equal", 0, 1452, 1452},
      {"default over", 0, 1484, 1452},    {"custom under", 1024, 1023, 1023},
      {"custom equal", 1024, 1024, 1024}, {"custom over", 1024, 1056, 1024},
  };

  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto config = baseConfig();
    config.set_max_syslog_msg_bytes(test_case.configured_limit);
    LoggerTestContext context(config, std::string(test_case.original_size - header.size(), 'x'));

    context.log();

    ASSERT_EQ(1, context.sender_ptr->messages_.size());
    EXPECT_EQ(test_case.expected_size, context.sender_ptr->messages_[0].size());
    EXPECT_THAT(context.sender_ptr->messages_[0], testing::StartsWith(header));
    const uint64_t truncated_bytes = test_case.original_size - test_case.expected_size;
    EXPECT_EQ(truncated_bytes == 0 ? 1 : 0, counterValue(context.store, "messages.state.full"));
    EXPECT_EQ(truncated_bytes == 0 ? 0 : 1,
              counterValue(context.store, "messages.state.truncated"));
    EXPECT_EQ(truncated_bytes, counterValue(context.store, "bytes_truncated"));
    EXPECT_EQ(0, counterValue(context.store, "send"));
    EXPECT_EQ(0, counterValue(context.store, "bytes_sent"));
  }
}

} // namespace
} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
