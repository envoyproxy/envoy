#include "source/extensions/stat_sinks/open_telemetry/request_splitter.h"

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

  auto* sm2 = rm->add_scope_metrics();
  auto* m3 = sm2->add_metrics();
  m3->set_name("m3");
  m3->mutable_histogram()->add_data_points()->set_count(4);

  // Total 4 datapoints. Max = 2 -> 2 requests
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 2, 0, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(2, requests.size());

  // Request 0: 2 datapoints (from m1)
  EXPECT_EQ(1, requests[0]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[0]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(1, requests[0]->resource_metrics(0).scope_metrics_size());
  EXPECT_EQ(1, requests[0]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ("m1", requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).name());
  EXPECT_EQ(
      2, requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size());

  // Request 1: 2 datapoints (from m2 and m3)
  EXPECT_EQ(1, requests[1]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[1]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(2, requests[1]->resource_metrics(0).scope_metrics_size());

  EXPECT_EQ(1, requests[1]->resource_metrics(0).scope_metrics(0).metrics_size());
  EXPECT_EQ("m2", requests[1]->resource_metrics(0).scope_metrics(0).metrics(0).name());
  EXPECT_EQ(1,
            requests[1]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points_size());

  EXPECT_EQ(1, requests[1]->resource_metrics(0).scope_metrics(1).metrics_size());
  EXPECT_EQ("m3", requests[1]->resource_metrics(0).scope_metrics(1).metrics(0).name());
  EXPECT_EQ(
      1,
      requests[1]->resource_metrics(0).scope_metrics(1).metrics(0).histogram().data_points_size());
}

TEST(RequestSplitterTest, MaxResourceMetricsPerRequest) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  for (int i = 0; i < 3; ++i) {
    auto* rm = resource_metrics.Add();
    rm->mutable_resource()->add_attributes()->set_key("rm" + std::to_string(i));
    rm->add_scope_metrics()->add_metrics()->mutable_gauge()->add_data_points();
  }

  // Total 3 resource metrics. Max = 2 -> 2 requests
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 0, 2, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(2, requests.size());

  EXPECT_EQ(2, requests[0]->resource_metrics_size());
  EXPECT_EQ("rm0", requests[0]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ("rm1", requests[0]->resource_metrics(1).resource().attributes(0).key());

  EXPECT_EQ(1, requests[1]->resource_metrics_size());
  EXPECT_EQ("rm2", requests[1]->resource_metrics(0).resource().attributes(0).key());
}

TEST(RequestSplitterTest, BothLimits) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;

  // RM1: 3 datapoints
  auto* rm1 = resource_metrics.Add();
  rm1->mutable_resource()->add_attributes()->set_key("rm1");
  auto* m1 = rm1->add_scope_metrics()->add_metrics();
  m1->mutable_gauge()->add_data_points();
  m1->mutable_gauge()->add_data_points();
  m1->mutable_gauge()->add_data_points();

  // RM2: 1 datapoint
  auto* rm2 = resource_metrics.Add();
  rm2->mutable_resource()->add_attributes()->set_key("rm2");
  rm2->add_scope_metrics()->add_metrics()->mutable_gauge()->add_data_points();

  // RM3: 2 datapoints
  auto* rm3 = resource_metrics.Add();
  rm3->mutable_resource()->add_attributes()->set_key("rm3");
  auto* m3 = rm3->add_scope_metrics()->add_metrics();
  m3->mutable_gauge()->add_data_points();
  m3->mutable_gauge()->add_data_points();

  // Max dp = 2, Max rm = 2
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 2, 2, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });

  // Expected behaviour:
  // DP1-1 + DP1-2 -> Request 0 (DP limit reached)
  // DP1-3 + DP2-1 -> Request 1 (DP & RM limit reached, since RM1 + RM2 = 2 RMs)
  // DP3-1 + DP3-2 -> Request 2 (Finished)
  ASSERT_EQ(3, requests.size());

  EXPECT_EQ(1, requests[0]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[0]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(
      2, requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size());

  EXPECT_EQ(2, requests[1]->resource_metrics_size());
  EXPECT_EQ("rm1", requests[1]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(
      1, requests[1]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size());
  EXPECT_EQ("rm2", requests[1]->resource_metrics(1).resource().attributes(0).key());
  EXPECT_EQ(
      1, requests[1]->resource_metrics(1).scope_metrics(0).metrics(0).gauge().data_points_size());

  EXPECT_EQ(1, requests[2]->resource_metrics_size());
  EXPECT_EQ("rm3", requests[2]->resource_metrics(0).resource().attributes(0).key());
  EXPECT_EQ(
      2, requests[2]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size());
}

TEST(RequestSplitterTest, NoLimits) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  auto* rm = resource_metrics.Add();
  rm->add_scope_metrics()->add_metrics()->mutable_gauge()->add_data_points();

  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(resource_metrics, 0, 0, [&requests](MetricsExportRequestPtr req) {
    requests.push_back(std::move(req));
  });
  ASSERT_EQ(1, requests.size());
  EXPECT_EQ(1, requests[0]->resource_metrics_size());
}

TEST(RequestSplitterTest, EmptyMetrics) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::metrics::v1::ResourceMetrics> resource_metrics;
  std::vector<MetricsExportRequestPtr> requests;
  RequestSplitter::chunkRequests(
      resource_metrics, 10, 10,
      [&requests](MetricsExportRequestPtr req) { requests.push_back(std::move(req)); });
  ASSERT_EQ(0, requests.size());
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
