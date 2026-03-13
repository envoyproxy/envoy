#include "envoy/common/exception.h"

#include "source/common/stats/histogram_impl.h"
#include "source/extensions/stat_sinks/open_telemetry/metric_aggregator.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

using testing::ReturnRef;

class MetricAggregatorTests : public testing::Test {
public:
  void SetUp() override {
    metric_aggregator_ = std::make_unique<MetricAggregator>(true, 1000, 500, 200, 0, attributes_);
  }

  void setupAggregatorWithNoAggregation() {
    metric_aggregator_ = std::make_unique<MetricAggregator>(false, 1000, 500, 200, 0, attributes_);
  }

  void setupAggregatorWithMaxDatapoints(uint32_t max_dp, bool enable_metric_aggregation) {
    metric_aggregator_ = std::make_unique<MetricAggregator>(
        enable_metric_aggregation, 1000, 500, 200, max_dp, attributes_);
  }

  class CustomMockHistogramStatistics : public Stats::HistogramStatistics {
  public:
    std::string quantileSummary() const override { return ""; }
    std::string bucketSummary() const override { return ""; }
    const std::vector<double>& supportedQuantiles() const override { return supported_quantiles_; }
    const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }
    Stats::ConstSupportedBuckets& supportedBuckets() const override { return supported_buckets_; }
    const std::vector<uint64_t>& computedBuckets() const override { return computed_buckets_; }
    std::vector<uint64_t> computeDisjointBuckets() const override { return computed_buckets_; }
    uint64_t sampleCount() const override { return sample_count_; }
    uint64_t outOfBoundCount() const override { return 0; }
    double sampleSum() const override { return sample_sum_; }

    std::vector<double> supported_quantiles_;
    std::vector<double> computed_quantiles_;
    std::vector<double> supported_buckets_{};
    std::vector<uint64_t> computed_buckets_{};
    uint64_t sample_count_{0};
    double sample_sum_{0};
  };

  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attributes_;
  std::unique_ptr<MetricAggregator> metric_aggregator_;
  CustomMockHistogramStatistics custom_stats_;

  void mockHistogram(const std::vector<double>& supported_buckets,
                     const std::vector<uint64_t>& computed_buckets, uint64_t sample_count,
                     uint64_t sample_sum) {
    custom_stats_.supported_buckets_ = supported_buckets;
    custom_stats_.computed_buckets_ = computed_buckets;
    custom_stats_.sample_count_ = sample_count;
    custom_stats_.sample_sum_ = sample_sum;
  }
};

TEST_F(MetricAggregatorTests, AggregatesGauge) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  metric_aggregator_->addGauge("metric1", 10, attr1);
  metric_aggregator_->addGauge("metric1", 20, attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size(), 1);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .gauge()
                .data_points(0)
                .as_int(),
            30); // Sum of gauges
}

TEST_F(MetricAggregatorTests, NoAggregationGauge) {
  setupAggregatorWithNoAggregation();
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  metric_aggregator_->addGauge("metric1", 10, attr1);
  metric_aggregator_->addGauge("metric1", 20, attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).gauge().data_points_size(), 1);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(1).gauge().data_points_size(), 1);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .gauge()
                .data_points(0)
                .as_int(),
            10);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(1)
                .gauge()
                .data_points(0)
                .as_int(),
            20);
}

TEST_F(MetricAggregatorTests, AggregatesCounter) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  metric_aggregator_->addCounter("metric1", 10, 5,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_CUMULATIVE,
                                 attr1);
  metric_aggregator_->addCounter("metric1", 20, 15,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_CUMULATIVE,
                                 attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points_size(),
            1);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points(0).as_int(),
      30); // Sum of cumulative counters

  metric_aggregator_ = std::make_unique<MetricAggregator>(true, 1000, 500, 200, 0, attributes_);
  metric_aggregator_->addCounter("metric2", 10, 5,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_DELTA,
                                 attr1);
  metric_aggregator_->addCounter("metric2", 20, 15,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_DELTA,
                                 attr1);

  requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points_size(),
            1);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points(0).as_int(),
      20); // Sum of deltas (5 + 15)
}

TEST_F(MetricAggregatorTests, NoAggregationCounter) {
  setupAggregatorWithNoAggregation();
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  metric_aggregator_->addCounter("metric1", 10, 5,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_CUMULATIVE,
                                 attr1);
  metric_aggregator_->addCounter("metric1", 20, 15,
                                 opentelemetry::proto::metrics::v1::AggregationTemporality::
                                     AGGREGATION_TEMPORALITY_CUMULATIVE,
                                 attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points_size(),
            1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics(1).sum().data_points_size(),
            1);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(0).sum().data_points(0).as_int(),
      10);
  EXPECT_EQ(
      requests[0]->resource_metrics(0).scope_metrics(0).metrics(1).sum().data_points(0).as_int(),
      20);
}

TEST_F(MetricAggregatorTests, AggregatesHistogram) {
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  std::vector<double> supported_buckets = {10, 20, 30};
  std::vector<uint64_t> computed_buckets1 = {1, 2, 3};
  std::vector<uint64_t> computed_buckets2 = {4, 5, 6};

  mockHistogram(supported_buckets, computed_buckets1, 6, 60);

  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_CUMULATIVE,
                                   attr1);

  mockHistogram(supported_buckets, computed_buckets2, 15, 150);
  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_CUMULATIVE,
                                   attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .histogram()
                .data_points_size(),
            1);
  auto dp = requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .histogram()
                .data_points(0);
  EXPECT_EQ(dp.count(), 21); // Sum of counts
  EXPECT_EQ(dp.sum(), 210);
  EXPECT_EQ(dp.bucket_counts_size(), 4);
  EXPECT_EQ(dp.bucket_counts(0), 5); // 1 + 4
  EXPECT_EQ(dp.bucket_counts(1), 7); // 2 + 5
  EXPECT_EQ(dp.bucket_counts(2), 9); // 3 + 6

  metric_aggregator_ = std::make_unique<MetricAggregator>(true, 1000, 500, 200, 0, attributes_);
  mockHistogram(supported_buckets, computed_buckets1, 6, 60);
  metric_aggregator_->addHistogram("metric2", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_DELTA,
                                   attr1);

  mockHistogram(supported_buckets, computed_buckets2, 15, 150);
  metric_aggregator_->addHistogram("metric2", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_DELTA,
                                   attr1);

  requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .histogram()
                .data_points_size(),
            1);
  dp = requests[0]
           ->resource_metrics(0)
           .scope_metrics(0)
           .metrics(0)
           .histogram()
           .data_points(0);
  EXPECT_EQ(dp.count(), 21); // Sum of deltas
  EXPECT_EQ(dp.sum(), 210);
  EXPECT_EQ(dp.bucket_counts_size(), 4);
  EXPECT_EQ(dp.bucket_counts(0), 5); // 1 + 4
  EXPECT_EQ(dp.bucket_counts(1), 7); // 2 + 5
  EXPECT_EQ(dp.bucket_counts(2), 9); // 3 + 6
}

TEST_F(MetricAggregatorTests, NoAggregationHistogram) {
  setupAggregatorWithNoAggregation();
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  std::vector<double> supported_buckets = {10, 20, 30};
  std::vector<uint64_t> computed_buckets1 = {1, 2, 3};
  std::vector<uint64_t> computed_buckets2 = {4, 5, 6};

  mockHistogram(supported_buckets, computed_buckets1, 6, 60);
  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_CUMULATIVE,
                                   attr1);

  mockHistogram(supported_buckets, computed_buckets2, 15, 150);
  metric_aggregator_->addHistogram("metric1", custom_stats_,
                                   opentelemetry::proto::metrics::v1::AggregationTemporality::
                                       AGGREGATION_TEMPORALITY_CUMULATIVE,
                                   attr1);

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .histogram()
                .data_points_size(),
            1);
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(1)
                .histogram()
                .data_points_size(),
            1);
  auto dp1 = requests[0]
                 ->resource_metrics(0)
                 .scope_metrics(0)
                 .metrics(0)
                 .histogram()
                 .data_points(0);
  EXPECT_EQ(dp1.count(), 6);
  EXPECT_EQ(dp1.sum(), 60);

  auto dp2 = requests[0]
                 ->resource_metrics(0)
                 .scope_metrics(0)
                 .metrics(1)
                 .histogram()
                 .data_points(0);
  EXPECT_EQ(dp2.count(), 15);
  EXPECT_EQ(dp2.sum(), 150);
}

TEST_F(MetricAggregatorTests, MaxDatapointsPerRequestNoAggregation) {
  setupAggregatorWithMaxDatapoints(2, false);
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  metric_aggregator_->addGauge("metric1", 10, attr1);
  metric_aggregator_->addGauge("metric2", 20, attr1);
  metric_aggregator_->addGauge("metric3", 30, attr1); // Should trigger a new request
  metric_aggregator_->addGauge("metric4", 40, attr1);
  metric_aggregator_->addGauge("metric5", 50, attr1); // Should trigger another

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 3);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[1]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[2]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);
}

TEST_F(MetricAggregatorTests, MaxDatapointsPerRequestWithAggregation) {
  setupAggregatorWithMaxDatapoints(2, true);
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr2;
  attr2.Add()->set_key("key2");

  metric_aggregator_->addGauge("metric1", 10, attr1); // Datapoint 1
  metric_aggregator_->addGauge("metric2", 20, attr1); // Datapoint 2 (Request 1 full)
  metric_aggregator_->addGauge("metric1", 15, attr1); // Aggregated, doesn't add to count
  metric_aggregator_->addGauge("metric3", 30, attr1); // Datapoint 3 (New request)
  metric_aggregator_->addGauge("metric1", 15, attr2); // Datapoint 4 (Different attribute)
  metric_aggregator_->addGauge("metric4", 40, attr1); // Datapoint 5 (New request)

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 3);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[1]->resource_metrics(0).scope_metrics(0).metrics_size(), 2);
  EXPECT_EQ(requests[2]->resource_metrics(0).scope_metrics(0).metrics_size(), 1);

  // metric1 with attr1 should have value 15
  EXPECT_EQ(requests[0]
                ->resource_metrics(0)
                .scope_metrics(0)
                .metrics(0)
                .gauge()
                .data_points(0)
                .as_int(),
            25);
}

TEST_F(MetricAggregatorTests, NoLimits) {
  setupAggregatorWithMaxDatapoints(0, false); // 0 means unlimited
  Protobuf::RepeatedPtrField<opentelemetry::proto::common::v1::KeyValue> attr1;
  attr1.Add()->set_key("key1");

  for (int i = 0; i < 1000; ++i) {
    metric_aggregator_->addGauge("metric" + std::to_string(i), i, attr1);
  }

  auto requests = metric_aggregator_->releaseRequests();
  EXPECT_EQ(requests.size(), 1);
  EXPECT_EQ(requests[0]->resource_metrics(0).scope_metrics(0).metrics_size(), 1000);
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
