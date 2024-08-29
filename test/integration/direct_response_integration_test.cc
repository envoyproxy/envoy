#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {
class DirectResponseIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                      public HttpIntegrationTest {
public:
  DirectResponseIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  // The default value for body size in bytes is 4096.
  void testDirectResponseBodySize(uint32_t body_size_bytes = 4096) {
    const std::string body_content(body_size_bytes, 'a');
    config_helper_.addConfigModifier(
        [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) -> void {
          auto* route_config = hcm.mutable_route_config();
          route_config->mutable_max_direct_response_body_size_bytes()->set_value(body_size_bytes);

          auto* route = route_config->mutable_virtual_hosts(0)->mutable_routes(0);

          route->mutable_match()->set_prefix("/direct");

          auto* direct_response = route->mutable_direct_response();
          direct_response->set_status(200);
          direct_response->mutable_body()->set_inline_string(body_content);
        });

    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/direct"},
        {":scheme", "http"},
        {":authority", "host"},
    });
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ(body_size_bytes, response->body().size());
    EXPECT_EQ(body_content, response->body());
  }

  // Test direct response with a file as the body.
  void testDirectResponseFile() {
    TestEnvironment::writeStringToFileForTest("file_direct.txt", "dummy");
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
    codec_client_ = makeHttpConnection(lookupPort("http"));

    auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/direct"},
        {":scheme", "http"},
        {":authority", "host"},
    });
    auto response = std::move(encoder_decoder.second);
    ASSERT_TRUE(response->waitForEndStream());
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    EXPECT_EQ("dummy", response->body());

    codec_client_->close();

    // Update the file and validate that the response is updated.
    TestEnvironment::writeStringToFileForTest("file_direct_updated.txt", "dummy-updated");
    TestEnvironment::renameFile(TestEnvironment::temporaryPath("file_direct_updated.txt"),
                                TestEnvironment::temporaryPath("file_direct.txt"));
    // This is needed to avoid a race between file rename, and the file being reloaded by data
    // source provider.
    timeSystem().realSleepDoNotUseWithoutScrutiny(std::chrono::milliseconds(10));
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto encoder_decoder_updated = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
        {":method", "POST"},
        {":path", "/direct"},
        {":scheme", "http"},
        {":authority", "host"},
    });
    auto updated_response = std::move(encoder_decoder_updated.second);
    ASSERT_TRUE(updated_response->waitForEndStream());
    ASSERT_TRUE(updated_response->complete());
    EXPECT_EQ("200", updated_response->headers().getStatusValue());
    EXPECT_EQ("dummy-updated", updated_response->body());
    codec_client_->close();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DirectResponseIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseBodySize) {
  // The default of direct response body size is 4KB.
  testDirectResponseBodySize();
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeLarge) {
  // Test with a large direct response body size, and with constrained buffer limits.
  config_helper_.setBufferLimits(1024, 1024);
  // Envoy takes much time to load the big configuration in TSAN mode and will result in the test to
  // be flaky. See https://github.com/envoyproxy/envoy/issues/33957 for more detail and context.
  // We reduce the body size from 4MB to 2MB to reduce the size of configuration to make the CI more
  // stable.
  testDirectResponseBodySize(/*1000*/ 500 * 4096);
}

TEST_P(DirectResponseIntegrationTest, DirectResponseBodySizeSmall) {
  testDirectResponseBodySize(1);
}

TEST_P(DirectResponseIntegrationTest, DefaultDirectResponseFile) { testDirectResponseFile(); }

} // namespace Envoy
