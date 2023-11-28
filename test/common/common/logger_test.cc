#include <memory>
#include <string>

#include "source/common/common/json_escape_string.h"
#include "source/common/common/logger.h"

#include "test/mocks/common.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Logger {
namespace {

TEST(LoggerTest, StackingStderrSinkDelegate) {
  StderrSinkDelegate stacked(Envoy::Logger::Registry::getSink());
}

TEST(LoggerEscapeTest, LinuxEOL) {
#ifdef _WIN32
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \n line 2\n"), "line 1 \\n line 2\\n");
#else
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \n line 2\n"), "line 1 \\n line 2\n");
#endif
}

TEST(LoggerEscapeTest, MultipleTrailingLinxEOL) {
#ifdef _WIN32
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line1\n\n"), "line1\\n\\n");
#else
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line1\n\n"), "line1\\n\n");
#endif
}

TEST(LoggerEscapeTest, WindowEOL) {
#ifdef _WIN32
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n"), "line 1 \\n line 2\r\n");
#else
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \n line 2\r\n"), "line 1 \\n line 2\\r\n");
#endif
}

TEST(LoggerEscapeTest, NoTrailingWhitespace) {
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \n line 2"), "line 1 \\n line 2");
}

TEST(LoggerEscapeTest, NoWhitespace) {
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line1"), "line1");
}

TEST(LoggerEscapeTest, AnyTrailingWhitespace) {
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("line 1 \t tab 1 \n line 2\t\t"),
            "line 1 \\t tab 1 \\n line 2\\t\\t");
}

TEST(LoggerEscapeTest, WhitespaceOnly) {
  // 8 spaces
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("        "), "        ");

  // Any whitespace characters
  EXPECT_EQ(DelegatingLogSink::escapeLogLine("\r\n\t \r\n \n\t"), "\\r\\n\\t \\r\\n \\n\\t");
}

TEST(LoggerEscapeTest, Empty) { EXPECT_EQ(DelegatingLogSink::escapeLogLine(""), ""); }

TEST(JsonEscapeTest, Escape) {
  const auto expect_json_escape = [](absl::string_view to_be_escaped, absl::string_view escaped) {
    EXPECT_EQ(escaped,
              JsonEscaper::escapeString(to_be_escaped, JsonEscaper::extraSpace(to_be_escaped)));
  };

  expect_json_escape("\"", "\\\"");
  expect_json_escape("\\", "\\\\");
  expect_json_escape("\b", "\\b");
  expect_json_escape("\f", "\\f");
  expect_json_escape("\n", "\\n");
  expect_json_escape("\r", "\\r");
  expect_json_escape("\t", "\\t");
  expect_json_escape("\x01", "\\u0001");
  expect_json_escape("\x02", "\\u0002");
  expect_json_escape("\x03", "\\u0003");
  expect_json_escape("\x04", "\\u0004");
  expect_json_escape("\x05", "\\u0005");
  expect_json_escape("\x06", "\\u0006");
  expect_json_escape("\x07", "\\u0007");
  expect_json_escape("\x08", "\\b");
  expect_json_escape("\x09", "\\t");
  expect_json_escape("\x0a", "\\n");
  expect_json_escape("\x0b", "\\u000b");
  expect_json_escape("\x0c", "\\f");
  expect_json_escape("\x0d", "\\r");
  expect_json_escape("\x0e", "\\u000e");
  expect_json_escape("\x0f", "\\u000f");
  expect_json_escape("\x10", "\\u0010");
  expect_json_escape("\x11", "\\u0011");
  expect_json_escape("\x12", "\\u0012");
  expect_json_escape("\x13", "\\u0013");
  expect_json_escape("\x14", "\\u0014");
  expect_json_escape("\x15", "\\u0015");
  expect_json_escape("\x16", "\\u0016");
  expect_json_escape("\x17", "\\u0017");
  expect_json_escape("\x18", "\\u0018");
  expect_json_escape("\x19", "\\u0019");
  expect_json_escape("\x1a", "\\u001a");
  expect_json_escape("\x1b", "\\u001b");
  expect_json_escape("\x1c", "\\u001c");
  expect_json_escape("\x1d", "\\u001d");
  expect_json_escape("\x1e", "\\u001e");
  expect_json_escape("\x1f", "\\u001f");
}

class LoggerCustomFlagsTest : public testing::TestWithParam<spdlog::logger*> {
public:
  LoggerCustomFlagsTest() : logger_(GetParam()) {}

  void expectLogMessage(const std::string& pattern, const std::string& message,
                        const std::string& expected) {
    StderrSinkDelegate stacked(Envoy::Logger::Registry::getSink());
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter
        ->add_flag<CustomFlagFormatter::EscapeMessageNewLine>(
            CustomFlagFormatter::EscapeMessageNewLine::Placeholder)
        .set_pattern(pattern);
    formatter
        ->add_flag<CustomFlagFormatter::EscapeMessageJsonString>(
            CustomFlagFormatter::EscapeMessageJsonString::Placeholder)
        .set_pattern(pattern);
    formatter
        ->add_flag<CustomFlagFormatter::ExtractedTags>(
            CustomFlagFormatter::ExtractedTags::Placeholder)
        .set_pattern(pattern);
    formatter
        ->add_flag<CustomFlagFormatter::ExtractedMessage>(
            CustomFlagFormatter::ExtractedMessage::Placeholder)
        .set_pattern(pattern);
    logger_->set_formatter(std::move(formatter));
    logger_->set_level(spdlog::level::info);

    testing::internal::CaptureStderr();
    logger_->log(spdlog::level::info, message);
    EXPECT_EQ(absl::StrCat(expected, TestEnvironment::newLine),
              testing::internal::GetCapturedStderr());
  }

protected:
  spdlog::logger* logger_;
};

INSTANTIATE_TEST_SUITE_P(EnvoyAndFineGrainLoggerFormatTest, LoggerCustomFlagsTest,
                         testing::ValuesIn(TestEnvironment::getSpdLoggersForTest()));

TEST_P(LoggerCustomFlagsTest, LogMessageAsIs) {
  // This uses "%v", the default flag for printing the actual text to log.
  // https://github.com/gabime/spdlog/wiki/3.-Custom-formatting#pattern-flags.
  expectLogMessage("%v", "\n\nmessage\n\n", "\n\nmessage\n\n");
}

TEST_P(LoggerCustomFlagsTest, LogMessageAsEscaped) {
  // This uses "%_", the added custom flag that escapes newlines from the actual text to log.
  expectLogMessage("%_", "\n\nmessage\n\n", "\\n\\nmessage\\n\\n");
}

TEST_P(LoggerCustomFlagsTest, LogMessageAsJsonStringEscaped) {
  // This uses "%j", the added custom flag that JSON escape the characters inside the log message
  // payload.
  expectLogMessage("%j", "message", "message");
  expectLogMessage("%j", "\n\nmessage\n\n", "\\n\\nmessage\\n\\n");
  expectLogMessage("%j", "\bok\b", "\\bok\\b");
  expectLogMessage("%j", "\fok\f", "\\fok\\f");
  expectLogMessage("%j", "\rok\r", "\\rok\\r");
  expectLogMessage("%j", "\tok\t", "\\tok\\t");
  expectLogMessage("%j", "\\ok\\", "\\\\ok\\\\");
  expectLogMessage("%j", "\"ok\"", "\\\"ok\\\"");
  expectLogMessage("%j", "\x01ok\x0e", "\\u0001ok\\u000e");
  expectLogMessage(
      "%j",
      "StreamAggregatedResources gRPC config stream closed: 14, connection error: desc = "
      "\"transport: Error while dialing dial tcp [::1]:15012: connect: connection refused\"",
      "StreamAggregatedResources gRPC config stream closed: 14, connection error: desc = "
      "\\\"transport: Error while dialing dial tcp [::1]:15012: connect: connection refused\\\"");
}

TEST_P(LoggerCustomFlagsTest, LogMessageWithTagsAndExtractMessage) {
  expectLogMessage("%+", "message", "message");
  expectLogMessage("%+", " message", " message");
  expectLogMessage("%+", "[Tags: \"key\":\"val\"] message1", "message1");
  expectLogMessage("%+", "[Tags: \"key1\":\"val1\",\"key2\":\"val2\"] message2", "message2");
  expectLogMessage("%+", "[Tags: \"key\":\"val\"] mes] sge3", "mes] sge3");
  expectLogMessage("%+", "[Tags: \"key\":\"val\"] mes\"] sge4", "mes\\\"] sge4");
}

TEST_P(LoggerCustomFlagsTest, LogMessageWithTagsAndExtractTags) {
  expectLogMessage("%*", "message1", "");
  expectLogMessage("%*", "[Tags: \"key\":\"val\"] message1", ",\"key\":\"val\"");
  expectLogMessage("%*", "[Tags: \"key1\":\"val1\",\"key2\":\"val2\"] message2",
                   ",\"key1\":\"val1\",\"key2\":\"val2\"");
  expectLogMessage("%*", "[Tags: \"key\":\"val\"] mes] sge3", ",\"key\":\"val\"");
  expectLogMessage("%*", "[Tags: \"key\":\"val\"] mes\"] sge4", ",\"key\":\"val\"");
}

class NamedLogTest : public Loggable<Id::assert>, public testing::Test {};

TEST_F(NamedLogTest, NamedLogsAreSentToSink) {
  MockLogSink sink(Envoy::Logger::Registry::getSink());

  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  // Log level is above debug, so we shouldn't get any logs.
  ENVOY_LOG_EVENT(debug, "test_event", "not logged");

  Envoy::Logger::Registry::setLogLevel(spdlog::level::debug);

  EXPECT_CALL(sink, log(_, _));
  EXPECT_CALL(sink, logWithStableName("test_event", "debug", "assert", "test log 1"));
  ENVOY_LOG_EVENT(debug, "test_event", "test {} {}", "log", 1);

  // Verify that ENVOY_LOG_EVENT_TO_LOGGER does the right thing.
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto log, const auto&) {
    EXPECT_TRUE(log.find("[misc]"));
  }));
  EXPECT_CALL(sink, logWithStableName("misc_event", "debug", "misc", "log"));
  ENVOY_LOG_EVENT_TO_LOGGER(Registry::getLog(Id::misc), debug, "misc_event", "log");
}

struct TlsLogSink : SinkDelegate {
  TlsLogSink(DelegatingLogSinkSharedPtr log_sink) : SinkDelegate(log_sink) { setTlsDelegate(); }
  ~TlsLogSink() override { restoreTlsDelegate(); }

  MOCK_METHOD(void, log, (absl::string_view, const spdlog::details::log_msg&));
  MOCK_METHOD(void, logWithStableName,
              (absl::string_view, absl::string_view, absl::string_view, absl::string_view));
  MOCK_METHOD(void, flush, ());
};

// Verifies that we can register a thread local sink override.
TEST(TlsLoggingOverrideTest, OverrideSink) {
  MockLogSink global_sink(Envoy::Logger::Registry::getSink());
  testing::InSequence s;

  {
    TlsLogSink tls_sink(Envoy::Logger::Registry::getSink());

    // Calls on the current thread goes to the TLS sink.
    EXPECT_CALL(tls_sink, log(_, _));
    ENVOY_LOG_MISC(info, "hello tls");

    // Calls on other threads should use the global sink.
    std::thread([&]() {
      EXPECT_CALL(global_sink, log(_, _));
      ENVOY_LOG_MISC(info, "hello global");
    }).join();

    // Sanity checking that we're still using the TLS sink.
    EXPECT_CALL(tls_sink, log(_, _));
    ENVOY_LOG_MISC(info, "hello tls");

    // All the logging functions should be delegated to the TLS override.
    EXPECT_CALL(tls_sink, flush());
    Registry::getSink()->flush();

    EXPECT_CALL(tls_sink, logWithStableName(_, _, _, _));
    Registry::getSink()->logWithStableName("foo", "level", "bar", "msg");
  }

  // Now that the TLS sink is out of scope, log calls on this thread should use the global sink
  // again.
  EXPECT_CALL(global_sink, log(_, _));
  ENVOY_LOG_MISC(info, "hello global 2");
}

TEST(LoggerTest, LogWithLogDetails) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);

  MockLogSink sink(Envoy::Logger::Registry::getSink());

  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_THAT(msg, HasSubstr("[info]"));
    EXPECT_THAT(msg, HasSubstr("hello"));

    EXPECT_EQ(log.logger_name, "misc");
  }));
  ENVOY_LOG_MISC(info, "hello");
}

TEST(LoggerTest, TestJsonFormatError) {
  ProtobufWkt::Any log_struct;
  log_struct.set_type_url("type.googleapis.com/bad.type.url");
  log_struct.set_value("asdf");

  // This scenario shouldn't happen in production, the test is added mainly for coverage.
  auto status = Envoy::Logger::Registry::setJsonLogFormat(log_struct);
  EXPECT_FALSE(status.ok());
  EXPECT_FALSE(Envoy::Logger::Registry::jsonLogFormatSet());
  EXPECT_EQ("INVALID_ARGUMENT: Provided struct cannot be serialized as JSON string",
            status.ToString());
}

TEST(LoggerTest, TestJsonFormatNonEscapedThrows) {
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);

  {
    ProtobufWkt::Struct log_struct;
    (*log_struct.mutable_fields())["Message"].set_string_value("%v");
    (*log_struct.mutable_fields())["NullField"].set_null_value(ProtobufWkt::NULL_VALUE);

    auto status = Envoy::Logger::Registry::setJsonLogFormat(log_struct);
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(Envoy::Logger::Registry::jsonLogFormatSet());
    EXPECT_EQ("INVALID_ARGUMENT: Usage of %v is unavailable for JSON log formats",
              status.ToString());
  }

  {
    ProtobufWkt::Struct log_struct;
    (*log_struct.mutable_fields())["Message"].set_string_value("%_");
    (*log_struct.mutable_fields())["NullField"].set_null_value(ProtobufWkt::NULL_VALUE);

    auto status = Envoy::Logger::Registry::setJsonLogFormat(log_struct);
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(Envoy::Logger::Registry::jsonLogFormatSet());
    EXPECT_EQ("INVALID_ARGUMENT: Usage of %_ is unavailable for JSON log formats",
              status.ToString());
  }
}

TEST(LoggerTest, TestJsonFormatEmptyStruct) {
  ProtobufWkt::Struct log_struct;
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_THAT(msg, HasSubstr("{}"));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "hello");
}

TEST(LoggerTest, TestJsonFormatNullAndFixedField) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  (*log_struct.mutable_fields())["FixedValue"].set_string_value("Fixed");
  (*log_struct.mutable_fields())["NullField"].set_null_value(ProtobufWkt::NULL_VALUE);
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto&) {
    EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
    EXPECT_THAT(msg, HasSubstr("\"Message\":\"hello\""));
    EXPECT_THAT(msg, HasSubstr("\"FixedValue\":\"Fixed\""));
    EXPECT_THAT(msg, HasSubstr("\"NullField\":null"));
  }));

  ENVOY_LOG_MISC(info, "hello");
}

TEST(LoggerTest, TestJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto& log) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"hello\""));
        EXPECT_EQ(log.logger_name, "misc");
      }))
      .WillOnce(Invoke([](auto msg, auto& log) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"hel\\nlo\""));
        EXPECT_EQ(log.logger_name, "misc");
      }))
      .WillOnce(Invoke([](auto msg, auto& log) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"hel\\\"lo\""));
        EXPECT_EQ(log.logger_name, "misc");
      }));

  ENVOY_LOG_MISC(info, "hello");
  ENVOY_LOG_MISC(info, "hel\nlo");
  ENVOY_LOG_MISC(info, "hel\"lo");
}

TEST(LoggerTest, TestJsonFormatWithNestedJsonMessage) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  (*log_struct.mutable_fields())["FixedValue"].set_string_value("Fixed");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
    EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
    EXPECT_THAT(msg, HasSubstr("\"Message\":\"{\\\"nested_message\\\":\\\"hello\\\"}\""));
    EXPECT_THAT(msg, HasSubstr("\"FixedValue\":\"Fixed\""));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "{\"nested_message\":\"hello\"}");
}

TEST(LoggerUtilityTest, TestSerializeLogTags) {
  // No entries
  EXPECT_EQ("", Envoy::Logger::Utility::serializeLogTags({}));

  // Empty key or value
  EXPECT_EQ("[Tags: \"\":\"\"] ", Envoy::Logger::Utility::serializeLogTags({{"", ""}}));
  EXPECT_EQ("[Tags: \"\":\"value\"] ", Envoy::Logger::Utility::serializeLogTags({{"", "value"}}));
  EXPECT_EQ("[Tags: \"key\":\"\"] ", Envoy::Logger::Utility::serializeLogTags({{"key", ""}}));

  // Single entry
  EXPECT_EQ("[Tags: \"key\":\"value\"] ",
            Envoy::Logger::Utility::serializeLogTags({{"key", "value"}}));

  // Multiple entries
  EXPECT_EQ("[Tags: \"key1\":\"value1\",\"key2\":\"value2\",\"key3\":\"value3\"] ",
            Envoy::Logger::Utility::serializeLogTags(
                {{"key1", "value1"}, {"key2", "value2"}, {"key3", "value3"}}));

  // Entries that require JSON escaping
  EXPECT_EQ("[Tags: "
            "\"ke\\\"y2\":\"value2\",\"ke\\\"y4\":\"va\\\"lue4\",\"key1\":\"value1\",\"key3\":"
            "\"va\\\"lue3\"] ",
            Envoy::Logger::Utility::serializeLogTags({{"key1", "value1"},
                                                      {"ke\"y2", "value2"},
                                                      {"key3", "va\"lue3"},
                                                      {"ke\"y4", "va\"lue4"}}));
}

class ClassForTaggedLog : public Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  ClassForTaggedLog() {
    EXPECT_CALL(connection_, id()).WillRepeatedly(Return(200));
    EXPECT_CALL(stream_, streamId()).WillRepeatedly(Return(300));
    EXPECT_CALL(stream_, connection())
        .WillRepeatedly(Return(makeOptRef(dynamic_cast<const Network::Connection&>(connection_))));
  }

  void logTaggedMessageWithPreCreatedTags() {
    ENVOY_TAGGED_LOG(info, tags_, "fake message {}", "val");
  }

  void logTaggedMessageWithInlineTags() {
    ENVOY_TAGGED_LOG(info, (std::map<std::string, std::string>{{"key_inline", "val"}}),
                     "fake message {}", "val");
  }

  void logTaggedMessageWithEscaping() {
    ENVOY_TAGGED_LOG(info, tags_with_escaping_, "fake me\"ssage {}", "val");
  }

  void logMessageWithConnection() { ENVOY_CONN_LOG(info, "fake message {}", connection_, "val"); }

  void logEventWithConnection() {
    ENVOY_CONN_LOG_EVENT(info, "test_event", "fake message {}", connection_, "val");
  }

  void logMessageWithStream() { ENVOY_STREAM_LOG(info, "fake message {}", stream_, "val"); }

  void logTaggedMessageWithConnection(std::map<std::string, std::string>& tags) {
    ENVOY_TAGGED_CONN_LOG(info, tags, connection_, "fake message");
  }

  void logTaggedMessageWithConnectionWithInlineTags() {
    ENVOY_TAGGED_CONN_LOG(info, (std::map<std::string, std::string>{{"key_inline", "val"}}),
                          connection_, "fake message");
  }

  void logTaggedMessageWithStream(std::map<std::string, std::string>& tags) {
    ENVOY_TAGGED_STREAM_LOG(info, tags, stream_, "fake message");
  }

  void logTaggedMessageWithStreamWithInlineTags() {
    ENVOY_TAGGED_STREAM_LOG(info, (std::map<std::string, std::string>{{"key_inline", "val"}}),
                            stream_, "fake message");
  }

private:
  std::map<std::string, std::string> tags_{{"key", "val"}};
  std::map<std::string, std::string> tags_with_escaping_{{"key", "val"}, {"ke\"y", "v\"al"}};
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
};

TEST(TaggedLogTest, TestTaggedLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"key\":\"val\"] fake message val"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"key_inline\":\"val\"] fake message val"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg,
                    HasSubstr("[Tags: \"ke\"y\":\"v\"al\",\"key\":\"val\"] fake me\"ssage val"));
      }));

  ClassForTaggedLog object;
  object.logTaggedMessageWithPreCreatedTags();
  object.logTaggedMessageWithInlineTags();
  object.logTaggedMessageWithEscaping();
}

TEST(TaggedLogTest, TestTaggedConnLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"ConnectionId\":\"200\"] fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg,
                    HasSubstr("[Tags: \"ConnectionId\":\"200\",\"key\":\"val\"] fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"ConnectionId\":\"105\"] fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg, HasSubstr("[Tags: \"ConnectionId\":\"200\",\"key_inline\":\"val\"] fake message"));
      }));

  std::map<std::string, std::string> empty_tags;
  std::map<std::string, std::string> tags{{"key", "val"}};
  std::map<std::string, std::string> tags_with_conn_id{{"ConnectionId", "105"}};

  ClassForTaggedLog object;
  object.logTaggedMessageWithConnection(empty_tags);
  object.logTaggedMessageWithConnection(tags);
  object.logTaggedMessageWithConnection(tags_with_conn_id);
  object.logTaggedMessageWithConnectionWithInlineTags();
}

TEST(TaggedLogTest, TestConnLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"ConnectionId\":\"200\"] fake message val"));
      }));

  ClassForTaggedLog object;
  object.logMessageWithConnection();
}

TEST(TaggedLogTest, TestConnEventLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(msg, HasSubstr("[Tags: \"ConnectionId\":\"200\"] fake message val"));
      }));

  EXPECT_CALL(sink, logWithStableName("test_event", "info", "filter",
                                      "[Tags: \"ConnectionId\":\"200\"] fake message val"));
  ClassForTaggedLog object;
  object.logEventWithConnection();
}

TEST(TaggedLogTest, TestConnEventLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }));

  EXPECT_CALL(sink, logWithStableName("test_event", "info", "filter",
                                      "[Tags: \"ConnectionId\":\"200\"] fake message val"));
  ClassForTaggedLog object;
  object.logEventWithConnection();
}

TEST(TaggedLogTest, TestTaggedStreamLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg, HasSubstr("[Tags: \"ConnectionId\":\"200\",\"StreamId\":\"300\"] fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg, HasSubstr("[Tags: \"ConnectionId\":\"200\",\"StreamId\":\"300\",\"key\":\"val\"] "
                           "fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg, HasSubstr("[Tags: \"ConnectionId\":\"200\",\"StreamId\":\"405\"] fake message"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg, HasSubstr(
                     "[Tags: \"ConnectionId\":\"200\",\"StreamId\":\"300\",\"key_inline\":\"val\"] "
                     "fake message"));
      }));

  std::map<std::string, std::string> empty_tags;
  std::map<std::string, std::string> tags{{"key", "val"}};
  std::map<std::string, std::string> tags_with_stream_id{{"StreamId", "405"}};

  ClassForTaggedLog object;
  object.logTaggedMessageWithStream(empty_tags);
  object.logTaggedMessageWithStream(tags);
  object.logTaggedMessageWithStream(tags_with_stream_id);
  object.logTaggedMessageWithStreamWithInlineTags();
}

TEST(TaggedLogTest, TestStreamLog) {
  Envoy::Logger::Registry::setLogFormat("%v");
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_THAT(
            msg,
            HasSubstr("[Tags: \"ConnectionId\":\"200\",\"StreamId\":\"300\"] fake message val"));
      }));

  ClassForTaggedLog object;
  object.logMessageWithStream();
}

TEST(TaggedLogTest, TestTaggedLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"key\":\"val\""));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake me\\\"ssage val\""));
        EXPECT_THAT(msg, HasSubstr("\"ke\\\"y\":\"v\\\"al\""));
        EXPECT_THAT(msg, HasSubstr("\"key\":\"val\""));
      }));

  ClassForTaggedLog object;
  object.logTaggedMessageWithPreCreatedTags();
  object.logTaggedMessageWithEscaping();
}

TEST(TaggedLogTest, TestTaggedConnLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"key\":\"val\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"key_inline\":\"val\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }));

  std::map<std::string, std::string> empty_tags;
  std::map<std::string, std::string> tags{{"key", "val"}};

  ClassForTaggedLog object;
  object.logTaggedMessageWithConnection(empty_tags);
  object.logTaggedMessageWithConnection(tags);
  object.logTaggedMessageWithConnectionWithInlineTags();
}

TEST(TaggedLogTest, TestConnLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }));

  ClassForTaggedLog object;
  object.logMessageWithConnection();
}

TEST(TaggedLogTest, TestTaggedStreamLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"StreamId\":\"300\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"key\":\"val\""));
        EXPECT_THAT(msg, HasSubstr("\"StreamId\":\"300\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message\""));
        EXPECT_THAT(msg, HasSubstr("\"key_inline\":\"val\""));
        EXPECT_THAT(msg, HasSubstr("\"StreamId\":\"300\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }));

  std::map<std::string, std::string> empty_tags;
  std::map<std::string, std::string> tags{{"key", "val"}};

  ClassForTaggedLog object;
  object.logTaggedMessageWithStream(empty_tags);
  object.logTaggedMessageWithStream(tags);
  object.logTaggedMessageWithStreamWithInlineTags();
}

TEST(TaggedLogTest, TestStreamLogWithJsonFormat) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"StreamId\":\"300\""));
        EXPECT_THAT(msg, HasSubstr("\"ConnectionId\":\"200\""));
      }));

  ClassForTaggedLog object;
  object.logMessageWithStream();
}

TEST(TaggedLogTest, TestTaggedLogWithJsonFormatMultipleJFlags) {
  ProtobufWkt::Struct log_struct;
  (*log_struct.mutable_fields())["Level"].set_string_value("%l");
  (*log_struct.mutable_fields())["Message1"].set_string_value("%j");
  (*log_struct.mutable_fields())["Message2"].set_string_value("%j");
  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  EXPECT_TRUE(Envoy::Logger::Registry::setJsonLogFormat(log_struct).ok());
  EXPECT_TRUE(Envoy::Logger::Registry::jsonLogFormatSet());

  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _))
      .WillOnce(Invoke([](auto msg, auto&) {
        // Using the mock connection write a log, skipping it.
        EXPECT_THAT(msg, HasSubstr("TestRandomGenerator"));
      }))
      .WillOnce(Invoke([](auto msg, auto&) {
        EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
        EXPECT_THAT(msg, HasSubstr("\"Level\":\"info\""));
        EXPECT_THAT(msg, HasSubstr("\"Message1\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"Message2\":\"fake message val\""));
        EXPECT_THAT(msg, HasSubstr("\"key\":\"val\""));
      }));

  ClassForTaggedLog object;
  object.logTaggedMessageWithPreCreatedTags();
}

} // namespace
} // namespace Logger
} // namespace Envoy
