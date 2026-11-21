#include "source/common/orca/orca_parser.h"

#include "test/extensions/filters/http/orca_reporter/fake_cgroup.h"
#include "test/integration/http_protocol_integration.h"

#include "absl/strings/substitute.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace OrcaReporter {
namespace {

std::string reportHeader(const Http::HeaderMap& headers) {
  const Http::HeaderMap::GetResult values =
      headers.get(Http::LowerCaseString("endpoint-load-metrics"));
  return values.empty() ? "" : std::string(values[0]->value().getStringView());
}

class OrcaReporterIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter(const std::string& extra_config = "") {
    cgroup_.setMemory(500, 100);
    cgroup_.setMemoryMax("1000");
    TestEnvironment::setEnvVar("ORCA_REPORTER_TEST_CGROUP", "integration", 1);
    config_helper_.prependFilter(absl::Substitute(R"EOF(
name: envoy.filters.http.orca_reporter
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.orca_reporter.v3.OrcaReporter
  metrics:
  - name: mem_utilization
    cgroup_memory:
      cgroups:
      - path: "$0/{ORCA_REPORTER_TEST_CGROUP}"
$1
)EOF",
                                                  TestEnvironment::temporaryPath("orca_reporter"),
                                                  extra_config));
    initialize();
  }

  // Sends a gRPC response with a body and the given trailers; trailers need HTTP/2 or later.
  IntegrationStreamDecoderPtr sendGrpcResponse(const Http::TestResponseTrailerMapImpl& trailers) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    auto response = codec_client_->makeHeaderOnlyRequest(default_request_headers_);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/grpc"}},
        false);
    upstream_request_->encodeData(10, false);
    upstream_request_->encodeTrailers(trailers);
    EXPECT_TRUE(response->waitForEndStream());
    return response;
  }

  bool trailersSupported() const {
    return downstreamProtocol() != Http::CodecType::HTTP1 &&
           upstreamProtocol() != Http::CodecType::HTTP1;
  }

  FakeCgroup cgroup_{"integration"};
};

INSTANTIATE_TEST_SUITE_P(Protocols, OrcaReporterIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams()),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(OrcaReporterIntegrationTest, AddsReportToResponse) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response =
      sendRequestAndWaitForResponse(default_request_headers_, 0, default_response_headers_, 0);
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("TEXT mem_utilization=0.4", reportHeader(response->headers()));
  const absl::StatusOr<xds::data::orca::v3::OrcaLoadReport> report =
      Orca::parseOrcaLoadReportHeaders(response->headers());
  ASSERT_TRUE(report.ok()) << report.status();
  EXPECT_DOUBLE_EQ(0.4, report->mem_utilization());
}

TEST_P(OrcaReporterIntegrationTest, KeepsUpstreamReport) {
  initializeFilter();
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestResponseHeaderMapImpl upstream_headers{
      {":status", "200"}, {"endpoint-load-metrics", "TEXT application_utilization=0.9"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, upstream_headers, 0);
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("TEXT application_utilization=0.9", reportHeader(response->headers()));
}

TEST_P(OrcaReporterIntegrationTest, OverwritesUpstreamReport) {
  initializeFilter("  overwrite_existing_report: true");
  codec_client_ = makeHttpConnection(lookupPort("http"));
  Http::TestResponseHeaderMapImpl upstream_headers{
      {":status", "200"}, {"endpoint-load-metrics", "TEXT application_utilization=0.9"}};
  auto response = sendRequestAndWaitForResponse(default_request_headers_, 0, upstream_headers, 0);
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("TEXT mem_utilization=0.4", reportHeader(response->headers()));
}

TEST_P(OrcaReporterIntegrationTest, AddsReportToGrpcTrailers) {
  if (!trailersSupported()) {
    return;
  }
  initializeFilter();
  auto response = sendGrpcResponse({{"grpc-status", "0"}});
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("", reportHeader(response->headers()));
  ASSERT_NE(nullptr, response->trailers());
  EXPECT_EQ("TEXT mem_utilization=0.4", reportHeader(*response->trailers()));
}

TEST_P(OrcaReporterIntegrationTest, KeepsUpstreamGrpcTrailerReport) {
  if (!trailersSupported()) {
    return;
  }
  initializeFilter();
  auto response = sendGrpcResponse(
      {{"grpc-status", "0"}, {"endpoint-load-metrics", "TEXT application_utilization=0.9"}});
  ASSERT_TRUE(response->complete());

  EXPECT_EQ("", reportHeader(response->headers()));
  ASSERT_NE(nullptr, response->trailers());
  EXPECT_EQ("TEXT application_utilization=0.9", reportHeader(*response->trailers()));
}

} // namespace
} // namespace OrcaReporter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
