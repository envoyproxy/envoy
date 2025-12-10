#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

class OpenTelemetryGrpcIntegrationTest : public Grpc::GrpcClientIntegrationParamTest,
                                         public HttpIntegrationTest {
public:
  using MetricsChecker = std::function<void(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&, bool&, bool&,
      bool&)>;

  OpenTelemetryGrpcIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, ipVersion()) {
    // TODO(ohadvano): add tag extraction rules.
    // Missing stat tag-extraction rule for stat 'grpc.otlp_collector.streams_closed_x' and
    // stat_prefix 'otlp_collector'.
    skip_tag_extraction_rule_check_ = true;
  }

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void setStatPrefix(const std::string& stat_prefix) { stat_prefix_ = stat_prefix; }

  const std::string getFullStatName(const std::string& stat_name) {
    if (stat_prefix_.empty()) {
      return stat_name;
    }

    return absl::StrCat(stat_prefix_, ".", stat_name);
  }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* otlp_collector_cluster = bootstrap.mutable_static_resources()->add_clusters();
      otlp_collector_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      otlp_collector_cluster->set_name("otlp_collector");
      ConfigHelper::setHttp2(*otlp_collector_cluster);

      auto* metrics_sink = bootstrap.add_stats_sinks();
      metrics_sink->set_name("envoy.stat_sinks.open_telemetry");
      envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
      setGrpcService(*sink_config.mutable_grpc_service(), "otlp_collector",
                     fake_upstreams_.back()->localAddress());
      sink_config.set_prefix(stat_prefix_);
      metrics_sink->mutable_typed_config()->PackFrom(sink_config);

      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(500));
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
                                                              otlp_collector_request_);
  }

  ABSL_MUST_USE_RESULT
  AssertionResult waitForMetricsRequest(const MetricsChecker& checker) {
    bool known_histogram_exists = false;
    bool known_counter_exists = false;
    bool known_gauge_exists = false;

    VERIFY_ASSERTION(waitForMetricsServiceConnection());

    while (!known_counter_exists || !known_gauge_exists || !known_histogram_exists) {
      VERIFY_ASSERTION(waitForMetricsStream());
      opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest export_request;
      VERIFY_ASSERTION(otlp_collector_request_->waitForGrpcMessage(*dispatcher_, export_request));
      EXPECT_EQ("POST", otlp_collector_request_->headers().getMethodValue());
      EXPECT_EQ("/opentelemetry.proto.collector.metrics.v1.MetricsService/Export",
                otlp_collector_request_->headers().getPathValue());
      EXPECT_EQ("application/grpc", otlp_collector_request_->headers().getContentTypeValue());

      EXPECT_EQ(1, export_request.resource_metrics().size());
      EXPECT_EQ(1, export_request.resource_metrics()[0].scope_metrics().size());
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>& metrics =
          export_request.resource_metrics()[0].scope_metrics()[0].metrics();
      EXPECT_TRUE(!metrics.empty());

      checker(metrics, known_counter_exists, known_gauge_exists, known_histogram_exists);

      // Since each export request creates a new stream, reply with an export response for each
      // export request.
      otlp_collector_request_->startGrpcStream();
      opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse export_response;
      otlp_collector_request_->sendGrpcMessage(export_response);
      otlp_collector_request_->finishGrpcStream(Grpc::Status::Ok);
    }

    EXPECT_TRUE(known_counter_exists);
    EXPECT_TRUE(known_gauge_exists);
    EXPECT_TRUE(known_histogram_exists);
    return AssertionSuccess();
  }

  void checkBasicMetrics(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>& metrics,
      bool& known_counter_exists, bool& known_gauge_exists, bool& known_histogram_exists) {
    long long int previous_time_stamp = 0;
    for (const opentelemetry::proto::metrics::v1::Metric& metric : metrics) {
      if (metric.name() == getFullStatName("cluster.membership_change") && metric.has_sum()) {
        known_counter_exists = true;
        EXPECT_EQ(1, metric.sum().data_points().size());
        EXPECT_EQ(1, metric.sum().data_points()[0].as_int());
        EXPECT_TRUE(metric.sum().data_points()[0].time_unix_nano() > 0);

        if (previous_time_stamp > 0) {
          EXPECT_EQ(previous_time_stamp, metric.sum().data_points()[0].time_unix_nano());
        }

        previous_time_stamp = metric.sum().data_points()[0].time_unix_nano();
      }

      if (metric.name() == getFullStatName("cluster.membership_total") && metric.has_gauge()) {
        known_gauge_exists = true;
        EXPECT_EQ(1, metric.gauge().data_points().size());
        EXPECT_EQ(1, metric.gauge().data_points()[0].as_int());
        EXPECT_TRUE(metric.gauge().data_points()[0].time_unix_nano() > 0);

        if (previous_time_stamp > 0) {
          EXPECT_EQ(previous_time_stamp, metric.gauge().data_points()[0].time_unix_nano());
        }

        previous_time_stamp = metric.gauge().data_points()[0].time_unix_nano();
      }

      if (metric.name() == getFullStatName("cluster.upstream_rq_time") && metric.has_histogram()) {
        known_histogram_exists = true;
        EXPECT_EQ(1, metric.histogram().data_points().size());
        EXPECT_EQ(metric.histogram().data_points()[0].bucket_counts().size(),
                  Stats::HistogramSettingsImpl::defaultBuckets().size() + 1);
        EXPECT_TRUE(metric.histogram().data_points()[0].time_unix_nano() > 0);

        previous_time_stamp = metric.histogram().data_points()[0].time_unix_nano();
      }
    }
  }

  void expectUpstreamRequestFinished() {
    switch (clientType()) {
    case Grpc::ClientType::EnvoyGrpc:
      test_server_->waitForGaugeEq("cluster.otlp_collector.upstream_rq_active", 0);
      break;
    case Grpc::ClientType::GoogleGrpc:
      test_server_->waitForCounterGe("grpc.otlp_collector.streams_closed_0", 1);
      break;
    default:
      PANIC("reached unexpected code");
    }
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
  FakeStreamPtr otlp_collector_request_;
  std::string stat_prefix_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OpenTelemetryGrpcIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(OpenTelemetryGrpcIntegrationTest, BasicFlow) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/path"}, {":scheme", "http"}, {":authority", "host"}};

  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(
      waitForMetricsRequest([this](const auto& metrics, auto& counter, auto& gauge, auto& hist) {
        checkBasicMetrics(metrics, counter, gauge, hist);
      }));

  expectUpstreamRequestFinished();
  cleanup();
}

TEST_P(OpenTelemetryGrpcIntegrationTest, BasicFlowWithStatPrefix) {
  setStatPrefix("prefix");
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/path"}, {":scheme", "http"}, {":authority", "host"}};

  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(
      waitForMetricsRequest([this](const auto& metrics, auto& counter, auto& gauge, auto& hist) {
        checkBasicMetrics(metrics, counter, gauge, hist);
      }));

  expectUpstreamRequestFinished();
  cleanup();
}

class OpenTelemetryGrpcIntegrationTestCustomConversion : public OpenTelemetryGrpcIntegrationTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* otlp_collector_cluster = bootstrap.mutable_static_resources()->add_clusters();
      otlp_collector_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      otlp_collector_cluster->set_name("otlp_collector");
      ConfigHelper::setHttp2(*otlp_collector_cluster);

      auto* metrics_sink = bootstrap.add_stats_sinks();
      metrics_sink->set_name("envoy.stat_sinks.open_telemetry");
      envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
      setGrpcService(*sink_config.mutable_grpc_service(), "otlp_collector",
                     fake_upstreams_.back()->localAddress());

      // Add custom conversion rules.
      Protobuf::TextFormat::ParseFromString(
          R"pb(matcher_list {
                     matchers {
                       predicate {
                         single_predicate {
                           input {
                             name: "stat_full_name_match_input"
                             typed_config {
                               [type.googleapis.com/
                     envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput] {}
                             }
                           }
                           value_match {
                             safe_regex { google_re2{} regex: ".*membership_change.*" }
                           }
                         }
                       }
                       on_match {
                         action {
                           name: "otlp_metric_conversion"
                           typed_config {
                             [type.googleapis.com/envoy.extensions.stat_sinks
                                  .open_telemetry.v3.SinkConfig
                                  .ConversionAction] {
                               metric_name: "custom.membership_change"
                             }
                           }
                         }
                       }
                     }
                     matchers {
                       predicate {
                         single_predicate {
                           input {
                             name: "stat_full_name_match_input"
                             typed_config {
                               [type.googleapis.com/
                     envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput] {}
                             }
                           }
                           value_match {
                             safe_regex { google_re2{}  regex: ".*membership_total.*" }
                           }
                         }
                       }
                       on_match {
                         action {
                           name: "otlp_metric_conversion"
                           typed_config {
                             [type.googleapis.com/envoy.extensions.stat_sinks
                                  .open_telemetry.v3.SinkConfig
                                  .ConversionAction] {
                               metric_name: "custom.membership_total"
                             }
                           }
                         }
                       }
                     }
                     matchers {
                       predicate {
                         single_predicate {
                           input {
                             name: "stat_full_name_match_input"
                             typed_config {
                               [type.googleapis.com/
                     envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput] {}
                             }
                           }
                           value_match {
                             safe_regex { google_re2{} regex: ".*upstream_rq_time.*" }
                           }
                         }
                       }
                       on_match {
                         action {
                           name: "otlp_metric_conversion"
                           typed_config {
                             [type.googleapis.com/envoy.extensions.stat_sinks
                                  .open_telemetry.v3.SinkConfig
                                  .ConversionAction] {
                               metric_name: "custom.upstream_rq_time"
                             }
                           }
                         }
                       }
                     }
                   })pb",
          sink_config.mutable_custom_metric_conversions());

      metrics_sink->mutable_typed_config()->PackFrom(sink_config);

      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(500));
    });

    HttpIntegrationTest::initialize();
  }

  void checkCustomMetrics(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>& metrics,
      bool& known_counter_exists, bool& known_gauge_exists, bool& known_histogram_exists) {
    for (const opentelemetry::proto::metrics::v1::Metric& metric : metrics) {
      // The metrics are aggregated into single metric with 2 data points, one for attribute
      // envoy.cluster_name="cluster_0" and envoy.cluster_name="otlp_collector".
      if (metric.name() == getFullStatName("custom.membership_change") && metric.has_sum()) {
        known_counter_exists = true;
        EXPECT_EQ(2, metric.sum().data_points().size());
        EXPECT_EQ(1, metric.sum().data_points()[0].as_int());
      }

      if (metric.name() == getFullStatName("custom.membership_total") && metric.has_gauge()) {
        known_gauge_exists = true;
        EXPECT_EQ(2, metric.gauge().data_points().size());
        EXPECT_EQ(1, metric.gauge().data_points()[0].as_int());
      }

      if (metric.name() == getFullStatName("custom.upstream_rq_time") && metric.has_histogram()) {
        known_histogram_exists = true;
        EXPECT_EQ(1, metric.histogram().data_points().size());
      }
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OpenTelemetryGrpcIntegrationTestCustomConversion,
                         GRPC_CLIENT_INTEGRATION_PARAMS,
                         Grpc::GrpcClientIntegrationParamTest::protocolTestParamsToString);

TEST_P(OpenTelemetryGrpcIntegrationTestCustomConversion, CustomConversionWithAggregation) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/path"}, {":scheme", "http"}, {":authority", "host"}};

  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(
      waitForMetricsRequest([this](const auto& metrics, auto& counter, auto& gauge, auto& hist) {
        checkCustomMetrics(metrics, counter, gauge, hist);
      }));

  expectUpstreamRequestFinished();
  cleanup();
}
} // namespace
} // namespace Envoy
