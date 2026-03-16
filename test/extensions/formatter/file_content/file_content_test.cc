#include "source/common/formatter/substitution_format_string.h"
#include "source/extensions/formatter/file_content/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

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

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
