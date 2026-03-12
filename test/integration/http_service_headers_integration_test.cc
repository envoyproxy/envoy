#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/extensions/access_loggers/open_telemetry/v3/logs_service.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/formatter/generic_secret/v3/generic_secret.pb.h"

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
    // Write the initial SDS YAML with the secret as an inline_string.
    writeSdsYaml("initial-token");
    const std::string sds_yaml_path = TestEnvironment::temporaryPath("otel_token_sds.yaml");

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* accesslog_cluster = bootstrap.mutable_static_resources()->add_clusters();
      accesslog_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      accesslog_cluster->set_name("accesslog");
      ConfigHelper::setHttp2(*accesslog_cluster);
    });

    config_helper_.addConfigModifier(
        [this, sds_yaml_path](
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

          // Authorization header using %SECRET(api-token)% from a file-based SDS source.
          auto* header_option = http->add_request_headers_to_add();
          auto* header = header_option->mutable_header();
          header->set_key("authorization");
          header->set_value("Bearer %SECRET(api-token)%");

          // Configure the generic_secret formatter extension at the HttpService level.
          auto* formatter_ext = http->add_formatters();
          formatter_ext->set_name("envoy.formatter.generic_secret");
          envoy::extensions::formatter::generic_secret::v3::GenericSecret generic_secret_cfg;
          auto& secret_cfg = (*generic_secret_cfg.mutable_secret_configs())["api-token"];
          secret_cfg.set_name("api-token");
          secret_cfg.mutable_sds_config()->mutable_path_config_source()->set_path(sds_yaml_path);
          formatter_ext->mutable_typed_config()->PackFrom(generic_secret_cfg);

          access_log->mutable_typed_config()->PackFrom(config);
        });

    HttpIntegrationTest::initialize();
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  // Write an SDS YAML file with the given token value.
  static void writeSdsYaml(const std::string& token) {
    TestEnvironment::writeStringToFileForTest("otel_token_sds.yaml", fmt::format(R"EOF(
resources:
- "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
  name: api-token
  generic_secret:
    secret:
      inline_string: "{}"
)EOF",
                                                                                 token));
  }

  // Rotate the secret by atomically replacing the SDS YAML file.
  // FilesystemSubscriptionImpl watches for MovedTo (rename) events only, so we
  // write to a temp file and rename to trigger the watcher.
  void rotateSecret(const std::string& new_token) {
    const std::string new_yaml = fmt::format(R"EOF(
resources:
- "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
  name: api-token
  generic_secret:
    secret:
      inline_string: "{}"
)EOF",
                                             new_token);
    TestEnvironment::writeStringToFileForTest("otel_token_sds.yaml.tmp", new_yaml);
    TestEnvironment::renameFile(TestEnvironment::temporaryPath("otel_token_sds.yaml.tmp"),
                                TestEnvironment::temporaryPath("otel_token_sds.yaml"));
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
};

INSTANTIATE_TEST_SUITE_P(IpVersions, HttpServiceHeadersRotationIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         [](const testing::TestParamInfo<Network::Address::IpVersion>& info) {
                           return TestUtility::ipVersionToString(info.param);
                         });

TEST_P(HttpServiceHeadersRotationIntegrationTest, SecretRotationReflectedInExportHeaders) {
  autonomous_upstream_ = true;
  initialize();

  // Trigger the first access log entry.
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_EQ("Bearer initial-token", captureAuthorizationHeader());

  // Rotate the secret and wait for the SDS update to propagate.
  rotateSecret("rotated-token");
  test_server_->waitForCounterGe("sds.api-token.update_success", 2);

  // Trigger a second access log entry; the flush will use the rotated secret value.
  auto response2 = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("Bearer rotated-token", captureAuthorizationHeader());

  codec_client_->close();
}

} // namespace
} // namespace Envoy
