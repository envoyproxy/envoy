#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/version/version.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"
#include "opentelemetry/proto/common/v1/common.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"
#include "opentelemetry/proto/resource/v1/resource.pb.h"

using testing::AssertionResult;

namespace Envoy {
namespace {

enum class ExporterType { GRPC, HTTP };

struct TransportDriver {
  std::function<void(envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig&,
                     Network::Address::InstanceConstSharedPtr)>
      configureExporter;
  std::function<AssertionResult(
      FakeStreamPtr&, Event::Dispatcher&,
      opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest&)>
      waitForRequest;
  std::function<void(FakeStreamPtr&)> sendResponse;
  std::function<void(IntegrationTestServer&)> expectUpstreamRequestFinished;
};

class OpenTelemetryIntegrationTest
    : public Grpc::BaseGrpcClientIntegrationParamTest,
      public testing::TestWithParam<
          std::tuple<Network::Address::IpVersion, Grpc::ClientType, ExporterType>>,
      public HttpIntegrationTest {
protected:
  TransportDriver driver_;

public:
  using MetricsChecker = std::function<void(
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>&, bool&, bool&,
      bool&)>;

  OpenTelemetryIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {
    // TODO(ohadvano): add tag extraction rules.
    // Missing stat tag-extraction rule for stat 'grpc.otlp_collector.streams_closed_x' and
    // stat_prefix 'otlp_collector'.
    skip_tag_extraction_rule_check_ = true;
    driver_ = (std::get<2>(GetParam()) == ExporterType::GRPC) ? makeGrpcDriver(clientType())
                                                              : makeHttpDriver();
  }

  Network::Address::IpVersion ipVersion() const override { return std::get<0>(GetParam()); }
  Grpc::ClientType clientType() const override { return std::get<1>(GetParam()); }

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
      driver_.configureExporter(sink_config, fake_upstreams_.back()->localAddress());
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
      VERIFY_ASSERTION(
          driver_.waitForRequest(otlp_collector_request_, *dispatcher_, export_request));

      EXPECT_EQ(1, export_request.resource_metrics().size());
      EXPECT_EQ(1, export_request.resource_metrics()[0].scope_metrics().size());
      const Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::Metric>& metrics =
          export_request.resource_metrics()[0].scope_metrics()[0].metrics();
      EXPECT_TRUE(!metrics.empty());

      checker(metrics, known_counter_exists, known_gauge_exists, known_histogram_exists);

      driver_.sendResponse(otlp_collector_request_);
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

  void cleanup() {
    if (fake_metrics_service_connection_ != nullptr) {
      AssertionResult result = fake_metrics_service_connection_->close();
      RELEASE_ASSERT(result, result.message());
      result = fake_metrics_service_connection_->waitForDisconnect();
      RELEASE_ASSERT(result, result.message());
    }
  }

private:
  TransportDriver makeGrpcDriver(Grpc::ClientType client_type) {
    return {[this](auto& config, auto addr) {
              setGrpcService(*config.mutable_grpc_service(), "otlp_collector", addr);
            },
            [](auto& stream, auto& dispatcher, auto& request) -> AssertionResult {
              VERIFY_ASSERTION(stream->waitForGrpcMessage(dispatcher, request));
              EXPECT_EQ("POST", stream->headers().getMethodValue());
              EXPECT_EQ("/opentelemetry.proto.collector.metrics.v1.MetricsService/Export",
                        stream->headers().getPathValue());
              EXPECT_EQ("application/grpc", stream->headers().getContentTypeValue());
              return AssertionSuccess();
            },
            [](auto& stream) {
              opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse response;
              stream->startGrpcStream();
              stream->sendGrpcMessage(response);
              stream->finishGrpcStream(Grpc::Status::Ok);
            },
            // GoogleGrpc uses its own stream tracking; EnvoyGrpc uses Envoy's cluster stats.
            [client_type](IntegrationTestServer& server) {
              if (client_type == Grpc::ClientType::GoogleGrpc) {
                server.waitForCounterGe("grpc.otlp_collector.streams_closed_0", 1);
              } else {
                server.waitForGaugeEq("cluster.otlp_collector.upstream_rq_active", 0);
              }
            }};
  }

  TransportDriver makeHttpDriver() {
    return {[this](auto& config, auto addr) {
              auto* http = config.mutable_http_service();
              http->mutable_http_uri()->set_uri(fmt::format(
                  "http://{}:{}/v1/metrics",
                  Network::Test::getLoopbackAddressUrlString(ipVersion()), addr->ip()->port()));
              http->mutable_http_uri()->set_cluster("otlp_collector");
              http->mutable_http_uri()->mutable_timeout()->set_seconds(1);
            },
            [](auto& stream, auto& dispatcher, auto& request) -> AssertionResult {
              VERIFY_ASSERTION(stream->waitForEndStream(dispatcher));
              EXPECT_EQ("POST", stream->headers().getMethodValue());
              EXPECT_EQ("/v1/metrics", stream->headers().getPathValue());
              EXPECT_EQ("application/x-protobuf", stream->headers().getContentTypeValue());
              EXPECT_TRUE(absl::StartsWith(stream->headers().getUserAgentValue(),
                                           "OTel-OTLP-Exporter-Envoy/"));
              EXPECT_TRUE(request.ParseFromString(stream->body().toString()));
              return AssertionSuccess();
            },
            [](auto& stream) {
              stream->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
            },
            // HTTP uses standard cluster request tracking.
            [](IntegrationTestServer& server) {
              server.waitForGaugeEq("cluster.otlp_collector.upstream_rq_active", 0);
            }};
  }

  FakeHttpConnectionPtr fake_metrics_service_connection_;
  FakeStreamPtr otlp_collector_request_;
  std::string stat_prefix_;
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeExporterType, OpenTelemetryIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     testing::Values(ExporterType::GRPC, ExporterType::HTTP)),
    [](const auto& info) {
      return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(info.param)),
                         std::get<1>(info.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                                 : "EnvoyGrpc",
                         std::get<2>(info.param) == ExporterType::GRPC ? "gRPC" : "HTTP");
    });

TEST_P(OpenTelemetryIntegrationTest, BasicFlow) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/path"}, {":scheme", "http"}, {":authority", "host"}};

  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(
      waitForMetricsRequest([this](const auto& metrics, auto& counter, auto& gauge, auto& hist) {
        checkBasicMetrics(metrics, counter, gauge, hist);
      }));

  driver_.expectUpstreamRequestFinished(*test_server_);
  cleanup();
}

TEST_P(OpenTelemetryIntegrationTest, BasicFlowWithStatPrefix) {
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

  driver_.expectUpstreamRequestFinished(*test_server_);
  cleanup();
}

class OpenTelemetryIntegrationTestCustomConversion : public OpenTelemetryIntegrationTest {
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

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeExporterType, OpenTelemetryIntegrationTestCustomConversion,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     testing::Values(ExporterType::GRPC)),
    [](const auto& info) {
      return fmt::format("{}_{}_{}", TestUtility::ipVersionToString(std::get<0>(info.param)),
                         std::get<1>(info.param) == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                                 : "EnvoyGrpc",
                         std::get<2>(info.param) == ExporterType::GRPC ? "gRPC" : "HTTP");
    });

TEST_P(OpenTelemetryIntegrationTestCustomConversion, CustomConversionWithAggregation) {
  initialize();

  codec_client_ = makeHttpConnection(makeClientConnection(lookupPort("http")));
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/path"}, {":scheme", "http"}, {":authority", "host"}};

  sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
  ASSERT_TRUE(
      waitForMetricsRequest([this](const auto& metrics, auto& counter, auto& gauge, auto& hist) {
        checkCustomMetrics(metrics, counter, gauge, hist);
      }));

  driver_.expectUpstreamRequestFinished(*test_server_);
  cleanup();
}
} // namespace
} // namespace Envoy
