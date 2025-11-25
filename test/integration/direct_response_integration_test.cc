#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class DirectResponseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DirectResponseIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void configureDirectResponseBody(absl::string_view body_content, uint32_t max_body_size_bytes) {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          route_config->mutable_max_direct_response_body_size_bytes()->set_value(
              max_body_size_bytes);

          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);

          route->mutable_match()->set_prefix("/direct");

          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(200);
          direct_response->mutable_body()->set_inline_string(body_content);
        });

    initialize();
  }

  std::unique_ptr<IntegrationStreamDecoder> getDirectBodyResponse() {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/direct"},
        {":scheme", "http"},
        {":authority", "host"},
    });
    auto response = std::move(encoder_decoder.second);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    return response;
  }

  void configureDirectResponseWithBodyFormat(absl::string_view body = "") {
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);

          route->mutable_match()->set_prefix("/direct");

          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(200);
          if (!body.empty()) {
            direct_response->mutable_body()->set_inline_string(body);
          }
          auto* body_format = direct_response->mutable_body_format();
          body_format->mutable_text_format_source()->set_inline_string(
              "prefix %LOCAL_REPLY_BODY% suffix");
        });

    initialize();
  }

  // Test direct response with a file as the body.
  void configureDirectResponseFile(absl::string_view content) {
    TestEnvironment::writeStringToFileForTest("file_direct.txt", std::string{content});
    const std::string filename = TestEnvironment::temporaryPath("file_direct.txt");
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);

          route->mutable_match()->set_prefix("/direct");

          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(200);
          direct_response->mutable_body()->set_filename(filename);
          direct_response->mutable_body()->mutable_watched_directory()->set_path(
              TestEnvironment::temporaryDirectory());
        });

    initialize();
  }

  void updateResponseFile(absl::string_view new_contents) {
    // Update the file and validate that the response is updated.
    TestEnvironment::writeStringToFileForTest("file_direct_updated.txt", std::string{new_contents});
    TestEnvironment::renameFile(TestEnvironment::temporaryPath("file_direct_updated.txt"),
                                TestEnvironment::temporaryPath("file_direct.txt"));
    // This is needed to avoid a race between file rename, and the file being reloaded by data
    // source provider.
    timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(10));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseBodySize) {
  // The default of direct response body size is 4KB.
  constexpr uint32_t size_bytes = 4 * 1024;
  const std::string body_content(size_bytes, 'a');
  configureDirectResponseBody(body_content, size_bytes);
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body_content, response->body());
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeLargeIgnoresBufferLimits) {
  // Test with a large direct response body size, and with constrained buffer limits.
  config_helper_.setBufferLimits(1024, 1024);
  // Envoy takes much time to load the big configuration in TSAN mode and will result in the test to
  // be flaky if the body size is 4MB.
  // See https://github.com/envoyproxy/envoy/issues/33957 for more detail and context.
  // So we use 2MB.
  constexpr uint32_t size_bytes = 2 * 1024 * 1024;
  const std::string body_content(size_bytes, 'a');
  configureDirectResponseBody(body_content, size_bytes);
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body_content, response->body());
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeSmall) {
  constexpr uint32_t size_bytes = 1;
  const std::string body_content(size_bytes, 'a');
  configureDirectResponseBody(body_content, size_bytes);
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body_content, response->body());
}

TEST_P(DirectResponseIntegrationTest, DirectResponseWithBodyFormatAndNoBody) {
  configureDirectResponseWithBodyFormat();
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("prefix  suffix", response->body());
}

TEST_P(DirectResponseIntegrationTest, DirectResponseWithBodyFormat) {
  configureDirectResponseWithBodyFormat("inner");
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("prefix inner suffix", response->body());
}

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseFileCanBeUpdated) {
  configureDirectResponseFile("dummy");
  auto response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("dummy", response->body());

  codec_client_->close();

  updateResponseFile("dummy-updated");

  response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("dummy-updated", response->body());
}

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseFileDoesNotUpdateBeyondSizeLimit) {
  configureDirectResponseFile("dummy");
  auto response = getDirectBodyResponse();
  codec_client_->close();

  // Default max size is 4096, so what if the file resizes to 4097?
  const std::string response_data(4097, 'a');
  updateResponseFile(response_data);

  response = getDirectBodyResponse();
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("dummy", response->body());
}

} // namespace Envoy
