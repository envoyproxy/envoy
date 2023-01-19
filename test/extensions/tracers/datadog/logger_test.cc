#include <datadog/error.h>
#include <datadog/string_view.h>

#include <memory>
#include <ostream>

#include "source/extensions/tracers/datadog/logger.h"

#include "absl/types/optional.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

class MockSink : public spdlog::sinks::sink {
public:
  ~MockSink() override = default;

  void log(const spdlog::details::log_msg& msg) override {
    payload_ = std::string{msg.payload.data(), msg.payload.size()};
  }

  void flush() override { flush_ = true; }

  void set_pattern(const std::string& pattern) override { pattern_ = pattern; }

  void set_formatter(std::unique_ptr<spdlog::formatter> sink_formatter) override {
    formatter_ = std::move(sink_formatter);
  }

  void reset() {
    payload_.reset();
    flush_ = false;
    pattern_.reset();
    formatter_.reset();
  }

  absl::optional<std::string> payload_;
  bool flush_ = false;
  absl::optional<std::string> pattern_;
  std::unique_ptr<spdlog::formatter> formatter_;
};

TEST(DatadogTracerLoggerTest, Logger) {
  const auto sink = std::make_shared<MockSink>();
  spdlog::logger spdlogger{"test", sink};
  Logger logger{spdlogger};

  // callback-style error
  logger.log_error([](std::ostream& log) { log << "Beware the ides of March."; });
  EXPECT_EQ("Beware the ides of March.", sink->payload_);
  EXPECT_EQ(absl::nullopt, sink->pattern_);
  EXPECT_EQ(nullptr, sink->formatter_);
  EXPECT_FALSE(sink->flush_);

  sink->reset();
  // callback-style startup banner
  logger.log_startup([](std::ostream& log) {
    log << "It's my stapler, the Swingline. It's been mine for a very long time.";
  });
  EXPECT_EQ("It's my stapler, the Swingline. It's been mine for a very long time.", sink->payload_);
  EXPECT_EQ(absl::nullopt, sink->pattern_);
  EXPECT_EQ(nullptr, sink->formatter_);
  EXPECT_FALSE(sink->flush_);

  sink->reset();
  // Error-style error
  logger.log_error(datadog::tracing::Error{datadog::tracing::Error::OTHER,
                                           "I'm sorry, Dave, I'm afraid I can't do that."});
  EXPECT_EQ("Datadog [error 1]: I'm sorry, Dave, I'm afraid I can't do that.", sink->payload_);
  EXPECT_EQ(absl::nullopt, sink->pattern_);
  EXPECT_EQ(nullptr, sink->formatter_);
  EXPECT_FALSE(sink->flush_);

  sink->reset();
  // string-style error
  logger.log_error("I must make my witness. I must lead the people from the waters. I must stay "
                   "their stampede to the sea.");
  EXPECT_EQ("I must make my witness. I must lead the people from the waters. I must stay their "
            "stampede to the sea.",
            sink->payload_);
  EXPECT_EQ(absl::nullopt, sink->pattern_);
  EXPECT_EQ(nullptr, sink->formatter_);
  EXPECT_FALSE(sink->flush_);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
