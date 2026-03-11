#include "source/extensions/stat_sinks/open_telemetry/request_splitter.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

TEST(RequestSplitterTest, MaxDatapointsPerRequest) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->mutable_resource()->add_attributes()->set_key("rm1");
  auto* sm = rm->add_scope_metrics();
  auto* m1 = sm->add_metrics();
  m1->set_name("m1");
  m1->mutable_gauge()->add_data_points()->set_as_int(1);
  m1->mutable_gauge()->add_data_points()->set_as_int(2);

  auto* m2 = sm->add_metrics();
  m2->set_name("m2");
  m2->mutable_sum()->add_data_points()->set_as_int(3);

  auto* m3 = sm->add_metrics();
  m3->set_name("m3");
  m3->mutable_histogram()->add_data_points()->set_count(4);

  // Total 4 data points. Max = 2 -> 2 requests
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 2, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(2, requests.size());

  // Request 0: 2 data points (from m1)
  EXPECT_EQ(1, requests[0]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[0]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(1, requests[0]->resource_metrics(0).scope_metrics_size());
  EXPECT_EQ(1, requests[0]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ("m1", requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).name());
  EXPECT_EQ(
      2, requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size());

  // Request 1: 2 data points (from m2 and m3)
  EXPECT_EQ(1, requests[1]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[1]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(1, requests[1]->resource_metrics(0).scope_metrics_size());

  EXPECT_EQ(2, requests[1]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ("m2", requests[1]->resource_metrics(0).scope_metrics(0).metrics(0).name());
  EXPECT_EQ(1,
            requests[1]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points_size());

  EXPECT_EQ("m3", requests[1]->resource_metrics(0).scope_metrics(0).metrics(1).name());
  EXPECT_EQ(
      1,
      requests[1]->resource_metrics(0).scope_metrics(0).metrics(1).histogram().data_points_size());
}

TEST(RequestSplitterTest, NoLimits) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->add_scope_metrics()->add_metrics()->mutable_gauge()->add_data_points();

  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 0, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(1, requests[0]->resource_metrics_size());
}

TEST(RequestSplitterTest, EmptyMetrics) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  resource_metrics.Add()->add_scope_metrics();
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 10, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(0, requests.size());
}

TEST(RequestSplitterTest, GaugeSplit) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->mutable_resource()->add_attributes()->set_key("rm_gauge");
  auto* m1 = rm->add_scope_metrics()->add_metrics();
  m1->set_name("m_gauge");
  m1->set_description("gauge description");
  m1->set_unit("1");
  m1->mutable_gauge()->add_data_points()->set_as_int(1);
  m1->mutable_gauge()->add_data_points()->set_as_int(2);

  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 1, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });

  ASSERT_EQ(2, requests.size());
  for (int i = 0; i < 2; i++) {
    auto& metric = requests[i]->resource_metrics(0).scope_metrics(0).metrics(0);
    EXPECT_EQ("m_gauge", metric.name());
    EXPECT_EQ("gauge description", metric.description());
    EXPECT_EQ("1", metric.unit());
    EXPECT_TRUE(metric.has_gauge());
    EXPECT_EQ(1, metric.gauge().data_points_size());
    EXPECT_EQ(i + 1, metric.gauge().data_points(0).as_int());
  }
}

TEST(RequestSplitterTest, SumSplit) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->mutable_resource()->add_attributes()->set_key("rm_sum");
  auto* m1 = rm->add_scope_metrics()->add_metrics();
  m1->set_name("m_sum");
  m1->set_description("sum description");
  m1->set_unit("1");
  m1->mutable_sum()->set_aggregation_temporality(
      opentelemetry::proto::metrics::v1::AggregationTemporality::
          AGGREGATION_TEMPORALITY_CUMULATIVE);
  m1->mutable_sum()->set_is_monotonic(true);

  m1->mutable_sum()->add_data_points()->set_as_int(1);
  m1->mutable_sum()->add_data_points()->set_as_int(2);

  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 1, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });

  ASSERT_EQ(2, requests.size());
  for (int i = 0; i < 2; i++) {
    auto& metric = requests[i]->resource_metrics(0).scope_metrics(0).metrics(0);
    EXPECT_EQ("m_sum", metric.name());
    EXPECT_EQ("sum description", metric.description());
    EXPECT_EQ("1", metric.unit());
    EXPECT_TRUE(metric.has_sum());
    EXPECT_EQ(opentelemetry::proto::metrics::v1::AggregationTemporality::
                  AGGREGATION_TEMPORALITY_CUMULATIVE,
              metric.sum().aggregation_temporality());
    EXPECT_TRUE(metric.sum().is_monotonic());
    EXPECT_EQ(1, metric.sum().data_points_size());
    EXPECT_EQ(i + 1, metric.sum().data_points(0).as_int());
  }
}

TEST(RequestSplitterTest, HistogramSplit) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->mutable_resource()->add_attributes()->set_key("rm_histogram");
  auto* m1 = rm->add_scope_metrics()->add_metrics();
  m1->set_name("m_histogram");
  m1->set_description("histogram description");
  m1->set_unit("ms");
  m1->mutable_histogram()->set_aggregation_temporality(
      opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA);

  m1->mutable_histogram()->add_data_points()->set_count(1);
  m1->mutable_histogram()->add_data_points()->set_count(2);

  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 1, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });

  ASSERT_EQ(2, requests.size());
  for (int i = 0; i < 2; i++) {
    auto& metric = requests[i]->resource_metrics(0).scope_metrics(0).metrics(0);
    EXPECT_EQ("m_histogram", metric.name());
    EXPECT_EQ("histogram description", metric.description());
    EXPECT_EQ("ms", metric.unit());
    EXPECT_TRUE(metric.has_histogram());
    EXPECT_EQ(
        opentelemetry::proto::metrics::v1::AggregationTemporality::AGGREGATION_TEMPORALITY_DELTA,
        metric.histogram().aggregation_temporality());
    EXPECT_EQ(1, metric.histogram().data_points_size());
    EXPECT_EQ(i + 1, metric.histogram().data_points(0).count());
  }
}

TEST(RequestSplitterTest, UnsupportedMetricTypes) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->mutable_resource()->add_attributes()->set_key("rm_exponential_histogram");
  auto* m1 = rm->add_scope_metrics()->add_metrics();
  m1->set_name("m_exponential_histogram");
  m1->mutable_exponential_histogram()->add_data_points();

  std::vector<MetricsExportRequestPtr> requests;
  EXPECT_ENVOY_BUG(RequestSplitter::chunkRequests(resource_metrics, 1,
                                                  [&requests](MetricsExportRequestPtr req) {
                                                    requests.push_back(std::move(req));
                                                  }),
                   "Metric type 10 is not supported in OTLP RequestSplitter.");

  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics2;
  auto* rm2 = resource_metrics2.Add();
  rm2->mutable_resource()->add_attributes()->set_key("rm_summary");
  auto* m2 = rm2->add_scope_metrics()->add_metrics();
  m2->set_name("m_summary");
  m2->mutable_summary()->add_data_points();

  EXPECT_ENVOY_BUG(RequestSplitter::chunkRequests(resource_metrics2, 1,
                                                  [&requests](MetricsExportRequestPtr req) {
                                                    requests.push_back(std::move(req));
                                                  }),
                   "Metric type 11 is not supported in OTLP RequestSplitter.");
}

TEST(RequestSplitterTest, MultipleResourceMetrics) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  resource_metrics.Add()->add_scope_metrics();
  resource_metrics.Add()->add_scope_metrics();

  std::vector<MetricsExportRequestPtr> requests;
  EXPECT_ENVOY_BUG(RequestSplitter::chunkRequests(resource_metrics, 1,
                                                  [&requests](MetricsExportRequestPtr req) {
                                                    requests.push_back(std::move(req));
                                                  }),
                   "Expected exactly 1 ResourceMetrics in OTLP stat sink.");
}

TEST(RequestSplitterTest, MultipleScopeMetrics) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->add_scope_metrics();
  rm->add_scope_metrics();

  std::vector<MetricsExportRequestPtr> requests;
  EXPECT_ENVOY_BUG(RequestSplitter::chunkRequests(resource_metrics, 1,
                                                  [&requests](MetricsExportRequestPtr req) {
                                                    requests.push_back(std::move(req));
                                                  }),
                   "Expected exactly 1 ScopeMetrics in OTLP stat sink ResourceMetrics.");
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
