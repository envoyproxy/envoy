#include "envoy/config/trace/v3/zipkin.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using envoy::config::trace::v3::ZipkinConfig;
using envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

// Integration test that verifies request_headers_to_add with a substitution formatter
// is correctly applied when the Zipkin tracer uses collector_service (HttpService).
class ZipkinHttpServiceIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                         public HttpIntegrationTest {
public:
  ZipkinHttpServiceIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  ~ZipkinHttpServiceIntegrationTest() override {
    if (connection_) {
      AssertionResult result = connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
      connection_.reset();
    }
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
    zipkin_upstream_ = fake_upstreams_.back().get();
  }

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* zipkin_cluster = bootstrap.mutable_static_resources()->add_clusters();
      zipkin_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      zipkin_cluster->set_name("zipkin");
      ConfigHelper::setHttp2(*zipkin_cluster);
    });

    config_helper_.addConfigModifier([this](HttpConnectionManager& hcm) -> void {
      HttpConnectionManager::Tracing tracing;
      tracing.mutable_random_sampling()->set_value(100);

      ZipkinConfig zipkin_config;
      auto* http_service = zipkin_config.mutable_collector_service();
      auto* http_uri = http_service->mutable_http_uri();
      http_uri->set_uri(fmt::format("http://{}:{}/api/v2/spans",
                                    Network::Test::getLoopbackAddressUrlString(GetParam()),
                                    zipkin_upstream_->localAddress()->ip()->port()));
      http_uri->set_cluster("zipkin");
      http_uri->mutable_timeout()->set_seconds(1);

      auto* header = http_service->add_request_headers_to_add();
      header->mutable_header()->set_key("x-custom-formatter");
      header->mutable_header()->set_value("%HOSTNAME%");

      zipkin_config.set_collector_endpoint_version(ZipkinConfig::HTTP_JSON);

      tracing.mutable_provider()->set_name("envoy.tracers.zipkin");
      tracing.mutable_provider()->mutable_typed_config()->PackFrom(zipkin_config);

      *hcm.mutable_tracing() = tracing;
    });

    HttpIntegrationTest::initialize();
  }

  FakeUpstream* zipkin_upstream_{};
  FakeHttpConnectionPtr connection_;
  FakeStreamPtr stream_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ZipkinHttpServiceIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ZipkinHttpServiceIntegrationTest, CollectorServiceWithFormatterHeader) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  codec_client_->close();

  // Wait for the zipkin span export HTTP request.
  // Zipkin flushes on a timer (default 5s) or when min_flush_spans is reached.
  // The runtime default is 5 spans, so we wait for the timer flush.
  ASSERT_TRUE(
      zipkin_upstream_->waitForHttpConnection(*dispatcher_, connection_, std::chrono::seconds(10)));
  ASSERT_TRUE(connection_->waitForNewStream(*dispatcher_, stream_, std::chrono::seconds(10)));
  ASSERT_TRUE(stream_->waitForEndStream(*dispatcher_, std::chrono::seconds(10)));

  // Verify the request was sent to the correct path.
  EXPECT_EQ("POST", stream_->headers().getMethodValue());
  EXPECT_EQ("/api/v2/spans", stream_->headers().getPathValue());

  // Verify the custom formatter header was applied.
  auto values = stream_->headers().get(Http::LowerCaseString("x-custom-formatter"));
  EXPECT_FALSE(values.empty());
  EXPECT_FALSE(values[0]->value().empty());
  EXPECT_NE(values[0]->value(), "%HOSTNAME%");

  stream_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "202"}}, true);
}

} // namespace Envoy
