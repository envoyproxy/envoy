// Integration test verifying that file-based FILE_CONTENT rotation is reflected in
// access log output via the substitution formatter.

#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class FileContentRotationIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  FileContentRotationIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    // Write the initial token to a plain file.
    token_path_ = TestEnvironment::temporaryPath("file_content_token.txt");
    TestEnvironment::writeStringToFileForTest("file_content_token.txt", "initial-token");

    // Build the file_content formatter extension config.
    envoy::config::core::v3::TypedExtensionConfig formatter_ext;
    formatter_ext.set_name("envoy.formatter.file_content");
    envoy::extensions::formatter::file_content::v3::FileContent file_content_cfg;
    formatter_ext.mutable_typed_config()->PackFrom(file_content_cfg);

    useAccessLog(fmt::format("%FILE_CONTENT({})%", token_path_), {formatter_ext});
    HttpIntegrationTest::initialize();
  }

  std::string token_path_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, FileContentRotationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(FileContentRotationIntegrationTest, FileRotationReflectedInAccessLog) {
  autonomous_upstream_ = true;
  initialize();

  // Trigger the first access log entry.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("initial-token", waitForAccessLog(access_log_name_));

  // Overwrite the token file. The DataSourceProvider uses Filesystem::Watcher::Events::Modified,
  // so a direct write triggers the re-read.
  TestEnvironment::writeStringToFileForTest("file_content_token.txt", "rotated-token");

  // Send requests until the file watcher propagates the update.
  for (uint32_t entry = 1;; ++entry) {
    auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    ASSERT_TRUE(response2->waitForEndStream());
    if (waitForAccessLog(access_log_name_, entry, true) == "rotated-token") {
      break;
    }
    absl::SleepFor(absl::Milliseconds(10));
  }

  codec_client_->close();
}

} // namespace
} // namespace Envoy
