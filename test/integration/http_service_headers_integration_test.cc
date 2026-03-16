#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/formatter/file_content/v3/file_content.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Verifies that formatted headers in envoy.config.core.v3.HttpService.request_headers_to_add are
// re-evaluated on each flush, so that formatters with external data can refresh.
//
// Uses the OTel HTTP access log exporter as the vehicle, since it is the
// simplest consumer of HttpServiceHeadersApplicator that sends HTTP requests
// whose headers we can inspect.
class HttpServiceHeadersRotationIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  HttpServiceHeadersRotationIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {
    skip_tag_extraction_rule_check_ = true;
  }

  void initialize() override {
    // Write the initial token file.
    token_path_ = TestEnvironment::writeStringToFileForTest("otel_api_token.txt", "initial-token");

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
    });

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
                hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.access_loggers.open_telemetry");

          envoy::extensions::access_loggers::open_telemetry::v3::OpenTelemetryAccessLogConfig
              config;
          config.set_log_name("test");
          // Flush immediately on every log entry.
          config.mutable_buffer_size_bytes()->set_value(1);

          auto* http = config.mutable_http_service();
          http->mutable_http_uri()->set_uri(fmt::format(
              "http://{}:{}/v1/logs", Network::Test::getLoopbackAddressUrlString(version_),
              fake_upstreams_.back()->localAddress()->ip()->port()));
          http->mutable_http_uri()->set_cluster("accesslog");
          http->mutable_http_uri()->mutable_timeout()->set_seconds(1);

          // Authorization header using %FILE_CONTENT(path)% for the token file.
          auto* header_option = http->add_request_headers_to_add();
          auto* header = header_option->mutable_header();
          header->set_key("authorization");
          header->set_value(fmt::format("Bearer %FILE_CONTENT({})%", token_path_));

          // FILE_CONTENT requires explicit formatter configuration.
          auto* formatter = http->add_formatters();
          formatter->set_name("envoy.formatter.file_content");
          envoy::extensions::formatter::file_content::v3::FileContent file_content_config;
          formatter->mutable_typed_config()->PackFrom(file_content_config);

          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  // Rotate the token by overwriting the file in place.
  // DataSourceProvider with modify_watch watches for Modified events.
  void rotateToken(const std::string& new_token) {
    TestEnvironment::writeStringToFileForTest("otel_api_token.txt", new_token);
  }

  std::string captureAuthorizationHeader() {
    FakeHttpConnectionPtr log_connection;
    FakeStreamPtr log_stream;
    EXPECT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, log_connection));
    EXPECT_TRUE(log_connection->waitForNewStream(*dispatcher_, log_stream));
    EXPECT_TRUE(log_stream->waitForEndStream(*dispatcher_));

    const auto& auth_entries = log_stream->headers().get(Http::LowerCaseString("authorization"));
    EXPECT_FALSE(auth_entries.empty());
    std::string auth_value =
        auth_entries.empty() ? "" : std::string(auth_entries[0]->value().getStringView());

    log_stream->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    EXPECT_TRUE(log_connection->close());
    EXPECT_TRUE(log_connection->waitForDisconnect());
    return auth_value;
  }

  std::string token_path_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpServiceHeadersRotationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(HttpServiceHeadersRotationIntegrationTest, FileRotationReflectedInExportHeaders) {
  autonomous_upstream_ = true;
  initialize();

  // Trigger the first access log entry.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("Bearer initial-token", captureAuthorizationHeader());

  // Rotate the token file and wait briefly for the file watcher to pick it up.
  rotateToken("rotated-token");
  timeSystem().advanceTimeWait(std::chrono::seconds(2));

  // Trigger a second access log entry; the flush will use the rotated token value.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("Bearer rotated-token", captureAuthorizationHeader());

  codec_client_->close();
}

} // namespace
} // namespace Envoy
