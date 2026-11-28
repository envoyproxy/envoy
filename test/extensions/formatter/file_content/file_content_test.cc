#include <fstream>

#include "envoy/filesystem/watcher.h"

#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/formatter/file_content/config.h"

#include "test/common/formatter/formatter_test_utility.h"
#include "test/mocks/filesystem/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class FileContentFormatterTest : public ::testing::Test {
public:
  FileContentFormatterTest() {
    // Wire up a real API and dispatcher so that DataSourceProvider can read files
    // and set up file watching.
    api_ = Api::createApiForTest();
    dispatcher_ = api_->allocateDispatcher("test_thread");

    ON_CALL(context_.server_factory_context_, mainThreadDispatcher())
        .WillByDefault(testing::ReturnRef(*dispatcher_));
    ON_CALL(context_.server_factory_context_, api()).WillByDefault(testing::ReturnRef(*api_));
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  StreamInfo::MockStreamInfo stream_info_;
  ::Envoy::Formatter::Context formatter_context_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;

  ::Envoy::Formatter::FormatterPtr makeFormatter(const std::string& yaml) {
    envoy::config::core::v3::SubstitutionFormatString config;
    TestUtility::loadFromYaml(yaml, config);
    return THROW_OR_RETURN_VALUE(
        ::Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig(config, context_),
        ::Envoy::Formatter::FormatterPtr);
  }
};

TEST_F(FileContentFormatterTest, ReadsFileContents) {
  const std::string file_path = TestEnvironment::writeStringToFileForTest("token.txt", "my-token");
  const std::string yaml = fmt::format(R"EOF(
text_format_source:
  inline_string: "Bearer %FILE_CONTENT({})%"
formatters:
- name: envoy.formatter.file_content
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent
)EOF",
                                       file_path);

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("Bearer my-token", formatter->format(formatter_context_, stream_info_));
}

TEST_F(FileContentFormatterTest, MissingFileThrows) {
  const std::string yaml = R"EOF(
text_format_source:
  inline_string: "prefix-%FILE_CONTENT(/nonexistent/path/to/file.txt)%-suffix"
formatters:
- name: envoy.formatter.file_content
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent
)EOF";

  EXPECT_THROW(makeFormatter(yaml), EnvoyException);
}

TEST_F(FileContentFormatterTest, MultipleFiles) {
  const std::string file1 = TestEnvironment::writeStringToFileForTest("file1.txt", "alpha");
  const std::string file2 = TestEnvironment::writeStringToFileForTest("file2.txt", "bravo");
  const std::string yaml = fmt::format(R"EOF(
text_format_source:
  inline_string: "%FILE_CONTENT({})%:%FILE_CONTENT({})%"
formatters:
- name: envoy.formatter.file_content
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent
)EOF",
                                       file1, file2);

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("alpha:bravo", formatter->format(formatter_context_, stream_info_));
}

TEST_F(FileContentFormatterTest, MaxLengthWithinLimit) {
  const std::string file_path = TestEnvironment::writeStringToFileForTest("small.txt", "short");
  // max_length 100 is larger than file content (5 bytes), so it should succeed.
  const std::string yaml = fmt::format(R"EOF(
text_format_source:
  inline_string: "%FILE_CONTENT({0}):100%"
formatters:
- name: envoy.formatter.file_content
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent
)EOF",
                                       file_path);

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("short", formatter->format(formatter_context_, stream_info_));
}

TEST_F(FileContentFormatterTest, MaxLengthExceeded) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("large.txt", "this-content-is-too-long");
  // max_length 5 truncates the file content to 5 characters.
  const std::string yaml = fmt::format(R"EOF(
text_format_source:
  inline_string: "%FILE_CONTENT({0}):5%"
formatters:
- name: envoy.formatter.file_content
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.formatter.file_content.v3.FileContent
)EOF",
                                       file_path);

  auto formatter = makeFormatter(yaml);
  EXPECT_EQ("this-", formatter->format(formatter_context_, stream_info_));
}

TEST_F(FileContentFormatterTest, FormatValueReturnsStringValue) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("value_test.txt", "my-value");

  FileContentFormatterFactory factory;
  auto empty_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*empty_config, context_);

  auto provider = parser->parse("FILE_CONTENT", file_path, std::nullopt).value();
  ASSERT_NE(nullptr, provider);

  auto value = Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_);
  EXPECT_EQ("my-value", value.string_value());
}

TEST_F(FileContentFormatterTest, FormatValueMissingFileReturnsEmpty) {
  // Create a parser but parse a command for a non-FILE_CONTENT command — should return nullptr.
  FileContentFormatterFactory factory;
  auto empty_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*empty_config, context_);

  auto provider = parser->parse("NOT_FILE_CONTENT", "/some/path", std::nullopt).value();
  EXPECT_EQ(nullptr, provider);
}

TEST_F(FileContentFormatterTest, UnsetThreadLocalDataIsMissingValue) {
  const std::string file_path =
      TestEnvironment::writeStringToFileForTest("deferred.txt", "my-value");

  // Must be set before the provider is created, so that its slot->set() is deferred.
  context_.server_factory_context_.thread_local_.defer_data_ = true;

  FileContentFormatterFactory factory;
  auto proto_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*proto_config, context_);
  ASSERT_NE(nullptr, parser);
  auto provider = *parser->parse("FILE_CONTENT", file_path, std::nullopt);
  ASSERT_NE(nullptr, provider);

  // Covers both the string and the value paths, old and sink-based alike.
  EXPECT_EQ(std::nullopt,
            Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_));
  EXPECT_EQ(Protobuf::Value::KIND_NOT_SET,
            Envoy::Formatter::formatValueForTest(*provider, formatter_context_, stream_info_)
                .kind_case());
}

TEST_F(FileContentFormatterTest, WatchDirectoryUpdatesOnSymlinkSwap) {
  const std::string dir = TestEnvironment::temporaryPath("file_content_watch");
  TestEnvironment::createPath(dir);
  const std::string target = fmt::format("{}/target", dir);
  const std::string link = fmt::format("{}/link", dir);
  const std::string new_target = fmt::format("{}/new_target", dir);
  const std::string new_link = fmt::format("{}/new_link", dir);

  {
    std::ofstream file(target);
    file << "original";
  }
  TestEnvironment::createSymlink(target, link);

  {
    std::ofstream file(new_target);
    file << "updated";
  }
  TestEnvironment::createSymlink(new_target, new_link);

  FileContentFormatterFactory factory;
  auto empty_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*empty_config, context_);

  const std::string subcommand = fmt::format("{}:{}", link, dir);
  auto provider = parser->parse("FILE_CONTENT", subcommand, std::nullopt).value();
  ASSERT_NE(nullptr, provider);

  // Initial content.
  auto result = Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("original", *result);

  // Atomically swap the symlink by renaming new_link over link.
  TestEnvironment::renameFile(new_link, link);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Content should reflect the updated file.
  result = Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ("updated", *result);
}

TEST_F(FileContentFormatterTest, TooManyColonsThrows) {
  FileContentFormatterFactory factory;
  auto empty_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*empty_config, context_);

  EXPECT_THROW_WITH_REGEX(parser->parse("FILE_CONTENT", "/a:/b:/c", std::nullopt).value(),
                          EnvoyException, "FILE_CONTENT: expected format");
}

TEST_F(FileContentFormatterTest, WindowsDrivePrefixesAndColonValidation) {
  NiceMock<Server::Configuration::MockFactoryContext> mock_context;
  auto& mock_fs = mock_context.server_factory_context_.api_.file_system_;
  auto& mock_dispatcher = mock_context.server_factory_context_.dispatcher_;

  FileContentFormatterFactory factory;
  auto empty_config = factory.createEmptyConfigProto();
  auto parser = factory.createCommandParserFromProto(*empty_config, mock_context);

  auto expect_parsed = [&](absl::string_view subcommand, const std::string& expected_file,
                           const std::string& expected_watch_dir, const std::string& content) {
    EXPECT_CALL(mock_fs, fileReadToEnd(expected_file)).WillOnce(testing::Return(content));
    if (!expected_watch_dir.empty()) {
      auto* dir_watcher = new NiceMock<Filesystem::MockWatcher>();
      auto* file_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(mock_dispatcher, createFilesystemWatcher_())
          .WillOnce(testing::Return(dir_watcher))
          .WillOnce(testing::Return(file_watcher));
      EXPECT_CALL(*dir_watcher, addWatch(absl::StrCat(expected_watch_dir, "/"),
                                         Filesystem::Watcher::Events::MovedTo, testing::_))
          .WillOnce(testing::Return(absl::OkStatus()));
      EXPECT_CALL(*file_watcher,
                  addWatch(expected_file, Filesystem::Watcher::Events::Modified, testing::_))
          .WillOnce(testing::Return(absl::OkStatus()));
    } else {
      auto* file_watcher = new NiceMock<Filesystem::MockWatcher>();
      EXPECT_CALL(mock_dispatcher, createFilesystemWatcher_())
          .WillOnce(testing::Return(file_watcher));
      EXPECT_CALL(*file_watcher,
                  addWatch(expected_file, Filesystem::Watcher::Events::Modified, testing::_))
          .WillOnce(testing::Return(absl::OkStatus()));
    }

    auto provider = parser->parse("FILE_CONTENT", subcommand, std::nullopt).value();
    ASSERT_NE(nullptr, provider);
    auto result = Envoy::Formatter::formatForTest(*provider, formatter_context_, stream_info_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(content, *result);
  };

  expect_parsed(R"(C:\dir\file.txt:D:\watch)", R"(C:\dir\file.txt)", R"(D:\watch)", "win-bs");
  expect_parsed("c:/dir/file.txt:d:/watch", "c:/dir/file.txt", "d:/watch", "win-fs");
  expect_parsed(R"(C:\dir\only.txt)", R"(C:\dir\only.txt)", "", "win-single");
  // Drive-letter colon without a path separator is treated as a normal delimiter.
  expect_parsed("C:relative", "C", "relative", "rel-token");

  for (absl::string_view invalid :
       {":/watch", "/path:", R"(C:\dir\file.txt:)", "/a:/b:/c", R"(C:\dir\file.txt:D:\watch:extra)",
        R"(C:relative:D:\watch)", R"(C:\dir\file.txt:D:relative)"}) {
    EXPECT_THROW_WITH_REGEX(parser->parse("FILE_CONTENT", invalid, std::nullopt).value(),
                            EnvoyException, "FILE_CONTENT: expected format");
  }
}

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
