#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/stats/histogram_impl.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"
#include "library/common/extensions/stat_sinks/metrics_service/config.pb.h"
#include "library/common/extensions/stat_sinks/metrics_service/service.pb.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class EnvoyMobileMetricsServiceIntegrationTest
    : public Grpc::VersionedGrpcClientIntegrationParamTest,
      public HttpIntegrationTest {
public:
  EnvoyMobileMetricsServiceIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(FakeHttpConnection::Type::HTTP2);
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // metrics_service cluster for Envoy gRPC.
      auto* metrics_service_cluster = bootstrap.mutable_static_resources()->add_clusters();
      metrics_service_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      metrics_service_cluster->set_name("metrics_service");
      metrics_service_cluster->mutable_http2_protocol_options();
      // metrics_service gRPC service definition.
      auto* metrics_sink = bootstrap.add_stats_sinks();
      metrics_sink->set_name("envoy.stat_sinks.metrics_service.mobile");
      envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileMetricsServiceConfig config;
      setGrpcService(*config.mutable_grpc_service(), "metrics_service",
                     fake_upstreams_.back()->localAddress());
      metrics_sink->mutable_typed_config()->PackFrom(config);
      // Shrink reporting period down to 1s to make test not take forever.
      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    });

    HttpIntegrationTest::initialize();
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForMetricsServiceConnection() {
    return fake_upstreams_[1]->waitForHttpConnection(*dispatcher_,
                                                     fake_metrics_service_connection_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForMetricsStream() {
    return fake_metrics_service_connection_->waitForNewStream(*dispatcher_,
                                                              metrics_service_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForMetricsRequest() {
    bool known_summary_exists = false;
    bool known_histogram_exists = false;
    bool known_counter_exists = false;
    bool known_gauge_exists = false;

    // Sometimes stats do not come in the first flush cycle, this loop ensures that we wait till
    // required stats are flushed.
    // TODO(ramaraochavali): Figure out a more robust way to find out all required stats have been
    // flushed.
    while (!(known_counter_exists && known_gauge_exists && known_histogram_exists)) {
      envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsMessage
          request_msg;
      VERIFY_ASSERTION(metrics_service_request_->waitForGrpcMessage(*dispatcher_, request_msg));
      EXPECT_EQ("POST", metrics_service_request_->headers().getMethodValue());
      EXPECT_EQ("/envoymobile.extensions.stat_sinks.metrics_service.EnvoyMobileMetricsService/"
                "EnvoyMobileStreamMetrics",
                metrics_service_request_->headers().getPathValue());
      EXPECT_EQ("application/grpc", metrics_service_request_->headers().getContentTypeValue());
      EXPECT_TRUE(request_msg.envoy_metrics_size() > 0);

      // batch_id should be set, and it's random, therefore verifying the size.
      EXPECT_TRUE(request_msg.batch_id().size() > 0);

      const Protobuf::RepeatedPtrField<::io::prometheus::client::MetricFamily>& envoy_metrics =
          request_msg.envoy_metrics();

      int64_t previous_time_stamp = 0;
      for (const ::io::prometheus::client::MetricFamily& metrics_family : envoy_metrics) {
        if (metrics_family.name() == "cluster.cluster_0.membership_change" &&
            metrics_family.type() == ::io::prometheus::client::MetricType::COUNTER) {
          known_counter_exists = true;
          EXPECT_EQ(1, metrics_family.metric(0).counter().value());
        }
        if (metrics_family.name() == "cluster.cluster_0.membership_total" &&
            metrics_family.type() == ::io::prometheus::client::MetricType::GAUGE) {
          known_gauge_exists = true;
          EXPECT_EQ(1, metrics_family.metric(0).gauge().value());
        }
        if (metrics_family.name() == "cluster.cluster_0.upstream_rq_time" &&
            metrics_family.type() == ::io::prometheus::client::MetricType::SUMMARY) {
          known_summary_exists = true;
          Stats::HistogramStatisticsImpl empty_statistics;
          EXPECT_EQ(metrics_family.metric(0).summary().quantile_size(),
                    empty_statistics.supportedQuantiles().size());
        }
        if (metrics_family.name() == "cluster.cluster_0.upstream_rq_time" &&
            metrics_family.type() == ::io::prometheus::client::MetricType::HISTOGRAM) {
          known_histogram_exists = true;
          EXPECT_EQ(metrics_family.metric(0).histogram().bucket_size(),
                    Stats::HistogramSettingsImpl::defaultBuckets().size());
        }
        ASSERT(metrics_family.metric(0).has_timestamp_ms());
        // Validate that all metrics have the same timestamp.
        if (previous_time_stamp > 0) {
          EXPECT_EQ(previous_time_stamp, metrics_family.metric(0).timestamp_ms());
        }
        previous_time_stamp = metrics_family.metric(0).timestamp_ms();
        if (known_counter_exists && known_gauge_exists && known_histogram_exists) {
          break;
        }
      }
    }
    EXPECT_TRUE(known_counter_exists);
    EXPECT_TRUE(known_gauge_exists);
    EXPECT_TRUE(known_summary_exists);
    EXPECT_TRUE(known_histogram_exists);

    return AssertionSuccess();
  }

  void cleanup() {
    if (fake_metrics_service_connection_ != nullptr) {
      AssertionResult result = fake_metrics_service_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_metrics_service_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

  FakeHttpConnectionPtr fake_metrics_service_connection_;
  FakeStreamPtr metrics_service_request_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, EnvoyMobileMetricsServiceIntegrationTest,
                         VERSIONED_GRPC_CLIENT_INTEGRATION_PARAMS);

// Test a basic metric service flow.
TEST_P(EnvoyMobileMetricsServiceIntegrationTest, BasicFlow) {
  initialize();
  // Send an empty request so that histogram values merged for cluster_0.
  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"x-lyft-user-id", "123"}};
  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);

  ASSERT_TRUE(waitForMetricsServiceConnection());
  ASSERT_TRUE(waitForMetricsStream());
  ASSERT_TRUE(waitForMetricsRequest());

  // Send a response with bath_id set and then end the stream.
  // This should never happen but make sure nothing breaks and we make a new stream on a follow up
  // request.
  metrics_service_request_->startGrpcStream();
  envoymobile::extensions::stat_sinks::metrics_service::EnvoyMobileStreamMetricsResponse
      response_msg;
  response_msg.set_batch_id("mock-batch-id");
  metrics_service_request_->sendGrpcMessage(response_msg);
  metrics_service_request_->finishGrpcStream(Grpc::Status::Ok);

  // response_msg arrived with the set batch_id.
  EXPECT_EQ("mock-batch-id", response_msg.batch_id());

  switch (clientType()) {
  case Grpc::ClientType::EnvoyGrpc:
    test_server_->waitForGaugeEq("cluster.metrics_service.upstream_rq_active", 0);
    break;
  case Grpc::ClientType::GoogleGrpc:
    test_server_->waitForCounterGe("grpc.metrics_service.streams_closed_0", 1);
    break;
  default:
    PANIC("not implemented");
  }
  cleanup();
}

} // namespace
} // namespace Envoy
