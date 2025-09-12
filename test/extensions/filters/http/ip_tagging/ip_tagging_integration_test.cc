#include "test/integration/http_integration.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace {

class IpTaggingIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                 public HttpIntegrationTest {
public:
  IpTaggingIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}
};

INSTANTIATE_TEST_SUITE_P(IpVersions, IpTaggingIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

// Just IP tagging for now.
const std::string ExampleIpTaggingConfig = R"EOF(
  name: ip_tagging
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
    request_type: both
    ip_tags:
      - ip_tag_name: external_request
        ip_list:
          - {address_prefix: 1.2.3.4, prefix_len: 32}
)EOF";

// Make sure that Envoy starts up with an ip tagging filter.
TEST_P(IpTaggingIntegrationTest, IpTaggingV3StaticTypedStructConfig) {
  config_helper_.prependFilter(ExampleIpTaggingConfig);
  initialize();
}

TEST_P(IpTaggingIntegrationTest, FileBasedIpTaggingWithReload) {
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("ip_tagging_test"));

  const std::string yaml =
      fmt::format(R"EOF(
    name: ip_tagging
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
      request_type: both
      ip_tags_file_provider:
        ip_tags_refresh_rate: 1s
        ip_tags_datasource:
            filename: "{}"
            watched_directory:
              path: "{}"
  )EOF",
                  TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
                  TestEnvironment::temporaryPath("ip_tagging_test"));

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
 )EOF",
      true);
  config_helper_.prependFilter(TestEnvironment::substitute(yaml));
  initialize();
  test_server_->waitForCounterEq("ip_tagging_reload.reload_success", 1);

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "1.2.3.4"}});

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().EnvoyIpTags)[0]->value(),
            "external_request");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  timeSystem().advanceTimeWait(std::chrono::seconds(2));
  test_server_->waitForCounterGe("ip_tagging_reload.reload_success", 2);
  // Simulate file update.
  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: external_updated_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
 )EOF",
      true);

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml"));
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"));
  timeSystem().advanceTimeWait(std::chrono::seconds(1));

  response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "1.2.3.4"}});

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().EnvoyIpTags)[0]->value(),
            "external_updated_request");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Remove the files.
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());
}

TEST_P(IpTaggingIntegrationTest, IptaggingFilterWithReloadNoCrashOnLdsUpdate) {
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());

  TestEnvironment::createPath(TestEnvironment::temporaryPath("ip_tagging_test"));

  const std::string yaml =
      fmt::format(R"EOF(
    name: ip_tagging
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging
      request_type: both
      ip_tags_file_provider:
        ip_tags_refresh_rate: 1s
        ip_tags_datasource:
            filename: "{}"
            watched_directory:
              path: "{}"
  )EOF",
                  TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
                  TestEnvironment::temporaryPath("ip_tagging_test"));

  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: external_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
 )EOF",
      true);
  config_helper_.prependFilter(TestEnvironment::substitute(yaml));
  initialize();
  test_server_->waitForCounterEq("ip_tagging_reload.reload_success", 1);

  // LDS update to modify the listener and corresponding drain.
  {
    ConfigHelper new_config_helper(version_, config_helper_.bootstrap());
    new_config_helper.addConfigModifier(
        [](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto* listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          listener->mutable_listener_filters_timeout()->set_seconds(10);
        });
    new_config_helper.setLds("1");
    test_server_->waitForGaugeEq("listener_manager.total_listeners_active", 1);
    test_server_->waitForCounterEq("listener_manager.lds.update_success", 2);
    test_server_->waitForGaugeEq("listener_manager.total_listeners_draining", 0);
  }
  timeSystem().advanceTimeWait(std::chrono::seconds(2));
  test_server_->waitForCounterGe("ip_tagging_reload.reload_success", 2);

  // Simulate file update.
  TestEnvironment::writeStringToFileForTest(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"), R"EOF(
ip_tags:
  - ip_tag_name: external_updated_request
    ip_list:
      - {address_prefix: 1.2.3.4, prefix_len: 32}
 )EOF",
      true);

  // Update the symlink to point to the new file.
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml"));
  TestEnvironment::renameFile(
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml"),
      TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml"));
  timeSystem().advanceTimeWait(std::chrono::seconds(1));
  test_server_->waitForCounterGe("ip_tagging_reload.reload_success", 3);
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "1.2.3.4"}});

  waitForNextUpstreamRequest();
  EXPECT_EQ(upstream_request_->headers().get(Http::Headers::get().EnvoyIpTags)[0]->value(),
            "external_updated_request");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());

  // Remove the files.
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_old_target.yaml").c_str());
  unlink(TestEnvironment::temporaryPath("ip_tagging_test/watcher_new_target.yaml").c_str());
}

} // namespace
} // namespace Envoy
