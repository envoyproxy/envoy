#include <memory>
#include <string>

#include "source/common/common/logger_delegates.h"
#include "source/common/protobuf/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/file_system_for_test.h"
#include "test/test_common/status_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::Envoy::StatusHelpers::StatusIs;
using testing::_;
using testing::ByMove;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Logger {
namespace {

class EventPipeDelegateTest : public testing::Test {
protected:
  EventPipeDelegateTest() { Envoy::Logger::Registry::setLogLevel(spdlog::level::info); }

  std::string readAndClose(filesystem_os_id_t read_file, size_t max_size) {
    char buf[1024];
    std::string data;
    while (data.size() < max_size) {
      auto bytes_read = ::read(read_file, buf, 1024);
      if (bytes_read <= 0) {
        break;
      }
      absl::StrAppend(&data, absl::string_view(buf, bytes_read));
    }
    ::close(read_file);
    return data;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
};

#ifndef WIN32
TEST_F(EventPipeDelegateTest, LogEvent) {
  EXPECT_CALL(context_.api_, fileSystem())
      .WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};

  // FIFO must be open for reading.
  auto read_file = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK, 0666);
  EXPECT_NE(-1, read_file);

  // Validate that logging events are propagated.
  MockLogSink sink(Registry::getSink());

  auto log_or_error = EventPipeDelegate::create(
      context_, fifo_path, xds::type::matcher::v3::Matcher(), Registry::getSink());
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
  const std::string expected_data = "[info] misc test_event 13\n";
  const std::string data = readAndClose(read_file, expected_data.size());
  EXPECT_EQ(expected_data, data);
}

TEST_F(EventPipeDelegateTest, LogEventWithFilter) {
  EXPECT_CALL(context_.api_, fileSystem())
      .WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log_filter");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};

  // FIFO must be open for reading.
  auto read_file = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK, 0666);
  EXPECT_NE(-1, read_file);

  xds::type::matcher::v3::Matcher matcher;
  MessageUtil::loadFromYaml(R"EOF(
    matcher_tree:
      input:
        name: event_name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.log_entry.v3.EventNameInput
      exact_match_map:
        map:
          "filtered_event":
            action:
              name: skip
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.matching.common_actions.v3.DropAction
  )EOF",
                            matcher, ProtobufMessage::getStrictValidationVisitor());

  MockLogSink sink(Registry::getSink());
  auto log_or_error = EventPipeDelegate::create(context_, fifo_path, matcher, Registry::getSink());
  EXPECT_OK(log_or_error);
  auto event_log = *std::move(log_or_error);

  EXPECT_CALL(sink, logWithStableName("filtered_event", "info", "misc", ""));
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "filtered_event", "");
  EXPECT_CALL(sink, logWithStableName("test_event", "info", "misc", "13"));
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "test_event", "{}", 13);
  event_log.reset();

  // Make sure filtered data is not present.
  const std::string expected_data = "[info] misc test_event 13\n";
  const std::string data = readAndClose(read_file, expected_data.size());
  EXPECT_EQ(expected_data, data);
}

TEST_F(EventPipeDelegateTest, LogEventNoReader) {
  EXPECT_CALL(context_.api_, fileSystem())
      .WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log_no_reader");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};
  auto log_or_error = EventPipeDelegate::create(
      context_, fifo_path, xds::type::matcher::v3::Matcher(), Registry::getSink());
  EXPECT_THAT(log_or_error, StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(EventPipeDelegateTest, LogEventReaderClosed) {
  EXPECT_CALL(context_.api_, fileSystem())
      .WillRepeatedly(ReturnRef(Filesystem::fileSystemForTest()));
  const std::string fifo_path = TestEnvironment::temporaryPath("event_log_close");
  ::mkfifo(fifo_path.c_str(), 0666);
  Cleanup cleanup{[fifo_path]() { ::unlink(fifo_path.c_str()); }};

  auto read_file = ::open(fifo_path.c_str(), O_RDONLY | O_NONBLOCK, 0666);
  EXPECT_NE(-1, read_file);
  auto log_or_error = EventPipeDelegate::create(
      context_, fifo_path, xds::type::matcher::v3::Matcher(), Registry::getSink());
  EXPECT_OK(log_or_error);
  auto event_log = *std::move(log_or_error);
  ::close(read_file);
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "test_event", "{}", 13);
  EXPECT_EQ(1U,
            context_.store_.findCounterByString("event_log.write_failed").value().get().value());
}

TEST_F(EventPipeDelegateTest, PartialWrite) {
  const std::string fifo_path = "fake_file";
  NiceMock<Filesystem::MockInstance> file_system;
  NiceMock<Filesystem::MockFile>* file = new NiceMock<Filesystem::MockFile>;
  EXPECT_CALL(file_system,
              createFile(testing::Matcher<const Envoy::Filesystem::FilePathAndType&>(
                  Filesystem::FilePathAndType{Filesystem::DestinationType::File, fifo_path})))
      .WillOnce(Return(ByMove(std::unique_ptr<NiceMock<Filesystem::MockFile>>(file))));
  EXPECT_CALL(context_.api_, fileSystem()).WillRepeatedly(ReturnRef(file_system));
  EXPECT_CALL(*file, open_(_)).WillOnce(Return(ByMove(Filesystem::resultSuccess<bool>(true))));
  EXPECT_CALL(*file, write_(_))
      .WillOnce(Invoke([&](absl::string_view data) -> Api::IoCallSizeResult {
        EXPECT_GT(data.size(), 4);
        return Filesystem::resultFailure<ssize_t>(4, EAGAIN);
      }));
  auto log_or_error = EventPipeDelegate::create(
      context_, fifo_path, xds::type::matcher::v3::Matcher(), Registry::getSink());
  EXPECT_OK(log_or_error);
  ENVOY_EVENT_TO_LOGGER(GET_MISC_LOGGER(), info, "test_event", "{}", 13);
  EXPECT_EQ(1U,
            context_.store_.findCounterByString("event_log.write_failed").value().get().value());
}
#endif

} // namespace
} // namespace Logger
} // namespace Envoy
