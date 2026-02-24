#include <memory>
#include <string>

#include "source/common/common/logger_delegates.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::StatusIs;
using testing::_;
using testing::ReturnRef;

namespace Envoy {
namespace Logger {
namespace {

class EventPipeDelegateTest : public testing::Test {
protected:
  EventPipeDelegateTest() { Envoy::Logger::Registry::setLogLevel(spdlog::level::info); }

  Stats::TestUtil::TestStore store_;
  Api::MockApi api_;
};

#ifndef WIN32
TEST_F(EventPipeDelegateTest, LogEvent) {
  EXPECT_CALL(api_, fileSystem()).WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};

  // FIFO must be open for reading.
  auto read_file = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK, 0666);
  EXPECT_NE(-1, read_file);

  // Validate that logging events are propagated.
  MockLogSink sink(Registry::getSink());

  auto log_or_error = EventPipeDelegate::create(api_, fifo_path, store_, Registry::getSink());
  EXPECT_OK(log_or_error);
  auto event_log = *std::move(log_or_error);

  // Should be filtered by the log level.
  ENVOY_LOG_MISC(trace, "below log level");
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), debug, "test_event", "below log level");

  // Log events must be propagated.
  EXPECT_CALL(sink, log(_, _));
  ENVOY_LOG_MISC(info, "hello");
  EXPECT_CALL(sink, logWithStableName("test_event", "info", "misc", "13"));
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "test_event", "{}", 13);
  event_log.reset();

  // Read the FIFO for the expected content.
  char buf[1024];
  const std::string expected_data = "[info] misc test_event 13\n";
  std::string data;
  while (data.size() < expected_data.size()) {
    auto bytes_read = ::read(read_file, buf, 1024);
    if (bytes_read <= 0) {
      break;
    }
    absl::StrAppend(&data, absl::string_view(buf, bytes_read));
  }
  EXPECT_EQ(expected_data, data);
}

TEST_F(EventPipeDelegateTest, LogEventNoReader) {
  EXPECT_CALL(api_, fileSystem()).WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log_no_reader");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};
  auto log_or_error = EventPipeDelegate::create(api_, fifo_path, store_, Registry::getSink());
  EXPECT_THAT(log_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(EventPipeDelegateTest, LogEventReaderClosed) {
  EXPECT_CALL(api_, fileSystem()).WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log_close");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};

  auto read_file = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK, 0666);
  EXPECT_NE(-1, read_file);
  auto log_or_error = EventPipeDelegate::create(api_, fifo_path, store_, Registry::getSink());
  EXPECT_OK(log_or_error);
  auto event_log = *std::move(log_or_error);
  ::close(read_file);
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "test_event", "{}", 13);
  EXPECT_EQ(1U, store_.findCounterByString("event_log.write_failed").value().get().value());
}
#endif

} // namespace
} // namespace Logger
} // namespace Envoy
