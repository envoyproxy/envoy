#include "envoy/grpc/async_client.h"
#include "envoy/service/metrics/v3/metrics_service.pb.h"

#include "source/extensions/stat_sinks/metrics_service/grpc_metrics_service_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/grpc/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/strings/str_format.h"
#include "io/prometheus/client/metrics.pb.h"

using namespace std::chrono_literals;
using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace MetricsService {
namespace {

class GrpcMetricsStreamerImplTest : public testing::Test {
public:
  using MockMetricsStream = Grpc::MockAsyncStream;
  using MetricsServiceCallbacks =
      Grpc::AsyncStreamCallbacks<envoy::service::metrics::v3::StreamMetricsResponse>;

  GrpcMetricsStreamerImplTest() : GrpcMetricsStreamerImplTest(0) {}

  explicit GrpcMetricsStreamerImplTest(uint32_t batch_size) {
    streamer_ = std::make_unique<GrpcMetricsStreamerImpl>(
        Grpc::RawAsyncClientSharedPtr{async_client_}, local_info_, batch_size);
  }

  void expectStreamStart(MockMetricsStream& stream, MetricsServiceCallbacks** callbacks_to_set) {
    EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
        .WillOnce(Invoke([&stream, callbacks_to_set](absl::string_view, absl::string_view,
                                                     Grpc::RawAsyncStreamCallbacks& callbacks,
                                                     const Http::AsyncClient::StreamOptions&) {
          *callbacks_to_set = dynamic_cast<MetricsServiceCallbacks*>(&callbacks);
          return &stream;
        }));
  }

  LocalInfo::MockLocalInfo local_info_;
  Grpc::MockAsyncClient* async_client_{new NiceMock<Grpc::MockAsyncClient>};
  GrpcMetricsStreamerImplPtr streamer_;
};

// Test basic metrics streaming flow.
TEST_F(GrpcMetricsStreamerImplTest, BasicFlow) {
  InSequence s;

  // Start a stream and send first message.
  MockMetricsStream stream1;
  MetricsServiceCallbacks* callbacks1;
  expectStreamStart(stream1, &callbacks1);
  EXPECT_CALL(local_info_, node());
  EXPECT_CALL(stream1, sendMessageRaw_(_, false));
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  streamer_->send(std::move(metrics));
  // Verify that sending an empty response message doesn't do anything bad.
  callbacks1->onReceiveMessage(
      std::make_unique<envoy::service::metrics::v3::StreamMetricsResponse>());
}

// Test that stream failure is handled correctly.
TEST_F(GrpcMetricsStreamerImplTest, StreamFailure) {
  InSequence s;

  EXPECT_CALL(*async_client_, startRaw(_, _, _, _))
      .WillOnce(
          Invoke([](absl::string_view, absl::string_view, Grpc::RawAsyncStreamCallbacks& callbacks,
                    const Http::AsyncClient::StreamOptions&) {
            callbacks.onRemoteClose(Grpc::Status::Internal, "bad");
            return nullptr;
          }));
  ON_CALL(local_info_, node()).WillByDefault(testing::ReturnRef(local_info_.node_));
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  streamer_->send(std::move(metrics));
}

class MockGrpcMetricsStreamer
    : public GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                                 envoy::service::metrics::v3::StreamMetricsResponse> {
public:
  MockGrpcMetricsStreamer(Grpc::RawAsyncClientSharedPtr async_client)
      : GrpcMetricsStreamer<envoy::service::metrics::v3::StreamMetricsMessage,
                            envoy::service::metrics::v3::StreamMetricsResponse>(async_client) {}

  // GrpcMetricsStreamer
  MOCK_METHOD(void, send, (MetricsPtr && metrics));
};

class MetricsServiceSinkTest : public testing::Test {
public:
  void addCounterToSnapshot(const std::string& name, uint64_t delta, uint64_t value,
                            bool used = true) {
    counter_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockCounter>>());
    counter_storage_.back()->name_ = name;
    counter_storage_.back()->value_ = value;
    counter_storage_.back()->used_ = used;

    snapshot_.counters_.push_back({delta, *counter_storage_.back()});
  }
  void addGaugeToSnapshot(const std::string& name, uint64_t value, bool used = true) {
    gauge_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockGauge>>());
    gauge_storage_.back()->name_ = name;
    gauge_storage_.back()->value_ = value;
    gauge_storage_.back()->used_ = used;

    snapshot_.gauges_.push_back(*gauge_storage_.back());
  }
  void addHistogramToSnapshot(const std::string& name, bool used = true) {
    histogram_storage_.emplace_back(std::make_unique<NiceMock<Stats::MockParentHistogram>>());
    histogram_storage_.back()->name_ = name;
    histogram_storage_.back()->used_ = used;

    snapshot_.histograms_.push_back(*histogram_storage_.back());
  }

  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockCounter>>> counter_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockGauge>>> gauge_storage_;
  std::vector<std::unique_ptr<NiceMock<Stats::MockParentHistogram>>> histogram_storage_;
  std::shared_ptr<MockGrpcMetricsStreamer> streamer_{new MockGrpcMetricsStreamer(
      Grpc::RawAsyncClientSharedPtr{new NiceMock<Grpc::MockAsyncClient>()})};
};

TEST_F(MetricsServiceSinkTest, CheckSendCall) {
  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, false, false,
           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

  addCounterToSnapshot("test_counter", 1, 1);
  addGaugeToSnapshot("test_gauge", 1);
  addHistogramToSnapshot("test_histogram");

  EXPECT_CALL(*streamer_, send(_));

  sink.flush(snapshot_);
}

TEST_F(MetricsServiceSinkTest, CheckStatsCount) {
  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, false, false,
           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

  addCounterToSnapshot("test_counter", 1, 100);
  addGaugeToSnapshot("test_gauge", 1);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    EXPECT_EQ(2, metrics->size());
  }));
  sink.flush(snapshot_);

  // Verify only newly added metrics come after endFlush call.
  gauge_storage_.back()->used_ = false;
  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    EXPECT_EQ(1, metrics->size());
  }));
  sink.flush(snapshot_);
}

// Test that verifies counters are correctly reported as current value when configured to do so.
TEST_F(MetricsServiceSinkTest, ReportCountersValues) {
  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, false, false,
           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

  addCounterToSnapshot("test_counter", 1, 100);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    EXPECT_EQ(1, metrics->size());
    EXPECT_EQ(100, (*metrics)[0].metric(0).counter().value());
  }));
  sink.flush(snapshot_);
}

// Test that verifies counters are reported as the delta between flushes when configured to do so.
TEST_F(MetricsServiceSinkTest, ReportCountersAsDeltas) {
  addCounterToSnapshot("test_counter", 1, 100);
  counter_storage_.back()->setTagExtractedName("tag-counter-name");
  counter_storage_.back()->setTags({{"a", "b"}});

  {
    // This test won't emit any labels.
    MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                       envoy::service::metrics::v3::StreamMetricsResponse>
        sink(streamer_, true, false,
             envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

    EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
      ASSERT_EQ(1, metrics->size());
      EXPECT_EQ("test_counter", (*metrics)[0].name());

      const auto& metric = (*metrics)[0].metric(0);
      EXPECT_EQ(1, metric.counter().value());
      EXPECT_EQ(0, metric.label().size());
    }));
    sink.flush(snapshot_);
  }

  {
    // This test will emit labels.
    MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                       envoy::service::metrics::v3::StreamMetricsResponse>
        sink(streamer_, true, true,
             envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

    EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
      ASSERT_EQ(1, metrics->size());
      EXPECT_EQ("tag-counter-name", (*metrics)[0].name());

      const auto& metric = (*metrics)[0].metric(0);
      EXPECT_EQ(1, metric.counter().value());
      EXPECT_EQ(1, metric.label().size());
    }));
    sink.flush(snapshot_);
  }
}

// Test the behavior of tag emission based on the emit_tags_as_label flag.
TEST_F(MetricsServiceSinkTest, ReportMetricsWithTags) {
  addCounterToSnapshot("full-counter-name", 1, 100);
  counter_storage_.back()->setTagExtractedName("tag-counter-name");
  counter_storage_.back()->setTags({{"a", "b"}});

  addGaugeToSnapshot("full-gauge-name", 100);
  gauge_storage_.back()->setTagExtractedName("tag-gauge-name");
  gauge_storage_.back()->setTags({{"a", "b"}});

  addHistogramToSnapshot("full-histogram-name");
  histogram_storage_.back()->setTagExtractedName("tag-histogram-name");
  histogram_storage_.back()->setTags({{"a", "b"}});

  {
    // When the emit_tags flag is false, we don't emit the tags and use the full name.
    MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                       envoy::service::metrics::v3::StreamMetricsResponse>
        sink(streamer_, false, false,
             envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

    EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
      EXPECT_EQ(4, metrics->size());

      EXPECT_EQ("full-counter-name", (*metrics)[0].name());
      EXPECT_EQ(0, (*metrics)[0].metric(0).label().size());

      EXPECT_EQ("full-gauge-name", (*metrics)[1].name());
      EXPECT_EQ(0, (*metrics)[1].metric(0).label().size());

      EXPECT_EQ("full-histogram-name", (*metrics)[2].name());
      EXPECT_EQ(0, (*metrics)[2].metric(0).label().size());

      EXPECT_EQ("full-histogram-name", (*metrics)[3].name());
      EXPECT_EQ(0, (*metrics)[3].metric(0).label().size());
    }));
    sink.flush(snapshot_);
  }

  io::prometheus::client::LabelPair expected_label_pair;
  expected_label_pair.set_name("a");
  expected_label_pair.set_value("b");

  // When the emit_tags flag is true, we emit the tags as labels and use the tag extracted name.
  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, false, true,
           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([&expected_label_pair](MetricsPtr&& metrics) {
    EXPECT_EQ(4, metrics->size());

    EXPECT_EQ("tag-counter-name", (*metrics)[0].name());
    EXPECT_EQ(1, (*metrics)[0].metric(0).label().size());
    EXPECT_TRUE(TestUtility::protoEqual(expected_label_pair, (*metrics)[0].metric(0).label()[0]));

    EXPECT_EQ("tag-gauge-name", (*metrics)[1].name());
    EXPECT_EQ(1, (*metrics)[1].metric(0).label().size());
    EXPECT_TRUE(TestUtility::protoEqual(expected_label_pair, (*metrics)[0].metric(0).label()[0]));

    EXPECT_EQ("tag-histogram-name", (*metrics)[2].name());
    EXPECT_EQ(1, (*metrics)[2].metric(0).label().size());
    EXPECT_TRUE(TestUtility::protoEqual(expected_label_pair, (*metrics)[0].metric(0).label()[0]));

    EXPECT_EQ("tag-histogram-name", (*metrics)[3].name());
    EXPECT_EQ(1, (*metrics)[3].metric(0).label().size());
    EXPECT_TRUE(TestUtility::protoEqual(expected_label_pair, (*metrics)[0].metric(0).label()[0]));
  }));
  sink.flush(snapshot_);
}

TEST_F(MetricsServiceSinkTest, FlushPredicate) {
  addCounterToSnapshot("used_counter", 100, 1);
  addCounterToSnapshot("unused_counter", 100, 1, false);

  // Default predicate only accepts used metrics.
  {
    MetricsFlusher flusher(true, true,
                           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);
    auto metrics = flusher.flush(snapshot_);
    EXPECT_EQ(1, metrics->size());
  }

  // Using a predicate that accepts all metrics, we'd flush both metrics.
  {
    MetricsFlusher flusher(true, true,
                           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM,
                           [](const auto&) { return true; });
    auto metrics = flusher.flush(snapshot_);
    EXPECT_EQ(2, metrics->size());
  }

  // Using a predicate that rejects all metrics, we'd flush no metrics.
  MetricsFlusher flusher(true, true,
                         envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM,
                         [](const auto&) { return false; });
  auto metrics = flusher.flush(snapshot_);
  EXPECT_EQ(0, metrics->size());
}

// This test will emit summary and histogram.
TEST_F(MetricsServiceSinkTest, HistogramEmitModeBoth) {
  addHistogramToSnapshot("test_histogram");

  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, true, false,
           envoy::config::metrics::v3::HistogramEmitMode::SUMMARY_AND_HISTOGRAM);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    ASSERT_EQ(2, metrics->size());
    EXPECT_EQ("test_histogram", (*metrics)[0].name());
    EXPECT_EQ("test_histogram", (*metrics)[1].name());

    const auto& metric1 = (*metrics)[0].metric(0);
    EXPECT_TRUE(metric1.has_summary());
    EXPECT_TRUE(metric1.summary().has_sample_sum());
    const auto& metric2 = (*metrics)[1].metric(0);
    EXPECT_TRUE(metric2.has_histogram());
  }));
  sink.flush(snapshot_);
}

// This test will only summary.
TEST_F(MetricsServiceSinkTest, HistogramEmitModeSummary) {
  addHistogramToSnapshot("test_histogram");

  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, true, false, envoy::config::metrics::v3::HistogramEmitMode::SUMMARY);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    ASSERT_EQ(1, metrics->size());
    EXPECT_EQ("test_histogram", (*metrics)[0].name());

    const auto& metric1 = (*metrics)[0].metric(0);
    EXPECT_TRUE(metric1.has_summary());
    EXPECT_TRUE(metric1.summary().has_sample_sum());
  }));
  sink.flush(snapshot_);
}

// This test will only histogram.
TEST_F(MetricsServiceSinkTest, HistogramEmitModeHistogram) {
  addHistogramToSnapshot("test_histogram");

  MetricsServiceSink<envoy::service::metrics::v3::StreamMetricsMessage,
                     envoy::service::metrics::v3::StreamMetricsResponse>
      sink(streamer_, true, false, envoy::config::metrics::v3::HistogramEmitMode::HISTOGRAM);

  EXPECT_CALL(*streamer_, send(_)).WillOnce(Invoke([](MetricsPtr&& metrics) {
    ASSERT_EQ(1, metrics->size());
    EXPECT_EQ("test_histogram", (*metrics)[0].name());

    const auto& metric1 = (*metrics)[0].metric(0);
    EXPECT_TRUE(metric1.has_histogram());
  }));
  sink.flush(snapshot_);
}

// Test batching with batch_size > 0
TEST_F(GrpcMetricsStreamerImplTest, BatchingWithMultipleBatches) {
  // Create a new async client for this test to avoid shared_ptr lifecycle issues
  auto batch_async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto batch_streamer =
      std::make_unique<GrpcMetricsStreamerImpl>(batch_async_client, local_info_, 2);

  InSequence s;

  // Start a stream and send batched metrics
  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  EXPECT_CALL(*batch_async_client, startRaw(_, _, _, _))
      .WillOnce(Invoke([&stream, &callbacks](absl::string_view, absl::string_view,
                                             Grpc::RawAsyncStreamCallbacks& cb,
                                             const Http::AsyncClient::StreamOptions&) {
        callbacks = dynamic_cast<MetricsServiceCallbacks*>(&cb);
        return &stream;
      }));

  // Identifier sent with first batch, then 2 more batches
  EXPECT_CALL(local_info_, node());
  // Expect 3 sendMessage calls (3 batches of 2, 2, 1 metrics)
  EXPECT_CALL(stream, sendMessageRaw_(_, false)).Times(3);

  // Create 5 metrics - should result in 3 batches (2, 2, 1)
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  for (int i = 0; i < 5; i++) {
    auto* metric = metrics->Add();
    metric->set_name(absl::StrFormat("metric_%d", i));
  }

  batch_streamer->send(std::move(metrics));
}

// Test batching when metrics count equals batch_size
TEST_F(GrpcMetricsStreamerImplTest, BatchingExactMatch) {
  auto batch_async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto batch_streamer =
      std::make_unique<GrpcMetricsStreamerImpl>(batch_async_client, local_info_, 3);

  InSequence s;

  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  EXPECT_CALL(*batch_async_client, startRaw(_, _, _, _))
      .WillOnce(Invoke([&stream, &callbacks](absl::string_view, absl::string_view,
                                             Grpc::RawAsyncStreamCallbacks& cb,
                                             const Http::AsyncClient::StreamOptions&) {
        callbacks = dynamic_cast<MetricsServiceCallbacks*>(&cb);
        return &stream;
      }));

  EXPECT_CALL(local_info_, node());
  // Should result in 1 batch (with identifier included)
  EXPECT_CALL(stream, sendMessageRaw_(_, false));

  // Create exactly 3 metrics
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  for (int i = 0; i < 3; i++) {
    auto* metric = metrics->Add();
    metric->set_name(absl::StrFormat("metric_%d", i));
  }

  batch_streamer->send(std::move(metrics));
}

// Test batching when metrics count is less than batch_size
TEST_F(GrpcMetricsStreamerImplTest, BatchingSmallerThanBatchSize) {
  auto batch_async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto batch_streamer =
      std::make_unique<GrpcMetricsStreamerImpl>(batch_async_client, local_info_, 100);

  InSequence s;

  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  EXPECT_CALL(*batch_async_client, startRaw(_, _, _, _))
      .WillOnce(Invoke([&stream, &callbacks](absl::string_view, absl::string_view,
                                             Grpc::RawAsyncStreamCallbacks& cb,
                                             const Http::AsyncClient::StreamOptions&) {
        callbacks = dynamic_cast<MetricsServiceCallbacks*>(&cb);
        return &stream;
      }));

  EXPECT_CALL(local_info_, node());
  // Should send all in one batch (with identifier included)
  EXPECT_CALL(stream, sendMessageRaw_(_, false));

  // Create only 5 metrics (less than batch_size)
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  for (int i = 0; i < 5; i++) {
    auto* metric = metrics->Add();
    metric->set_name(absl::StrFormat("metric_%d", i));
  }

  batch_streamer->send(std::move(metrics));
}

// Test no batching with batch_size = 0 (default behavior)
TEST_F(GrpcMetricsStreamerImplTest, NoBatchingWithZeroBatchSize) {
  // Default constructor uses batch_size = 0
  InSequence s;

  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  expectStreamStart(stream, &callbacks);

  EXPECT_CALL(local_info_, node());
  // Should send all in one message (no batching, with identifier included)
  EXPECT_CALL(stream, sendMessageRaw_(_, false));

  // Create many metrics
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  for (int i = 0; i < 1000; i++) {
    auto* metric = metrics->Add();
    metric->set_name(absl::StrFormat("metric_%d", i));
  }

  streamer_->send(std::move(metrics));
}

// Test empty metrics with batching
TEST_F(GrpcMetricsStreamerImplTest, BatchingWithEmptyMetrics) {
  auto batch_async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto batch_streamer =
      std::make_unique<GrpcMetricsStreamerImpl>(batch_async_client, local_info_, 10);

  InSequence s;

  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  EXPECT_CALL(*batch_async_client, startRaw(_, _, _, _))
      .WillOnce(Invoke([&stream, &callbacks](absl::string_view, absl::string_view,
                                             Grpc::RawAsyncStreamCallbacks& cb,
                                             const Http::AsyncClient::StreamOptions&) {
        callbacks = dynamic_cast<MetricsServiceCallbacks*>(&cb);
        return &stream;
      }));

  EXPECT_CALL(local_info_, node());
  // Should send one message (with identifier included)
  EXPECT_CALL(stream, sendMessageRaw_(_, false));

  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();

  batch_streamer->send(std::move(metrics));
}

// Test batching with batch_size = 1
TEST_F(GrpcMetricsStreamerImplTest, BatchingSizeOne) {
  auto batch_async_client = std::make_shared<NiceMock<Grpc::MockAsyncClient>>();
  auto batch_streamer =
      std::make_unique<GrpcMetricsStreamerImpl>(batch_async_client, local_info_, 1);

  InSequence s;

  MockMetricsStream stream;
  MetricsServiceCallbacks* callbacks;
  EXPECT_CALL(*batch_async_client, startRaw(_, _, _, _))
      .WillOnce(Invoke([&stream, &callbacks](absl::string_view, absl::string_view,
                                             Grpc::RawAsyncStreamCallbacks& cb,
                                             const Http::AsyncClient::StreamOptions&) {
        callbacks = dynamic_cast<MetricsServiceCallbacks*>(&cb);
        return &stream;
      }));

  EXPECT_CALL(local_info_, node());
  // Expect 3 batches (first one includes identifier)
  EXPECT_CALL(stream, sendMessageRaw_(_, false)).Times(3);

  // Create 3 metrics - should result in 3 batches of 1 each
  auto metrics =
      std::make_unique<Envoy::Protobuf::RepeatedPtrField<io::prometheus::client::MetricFamily>>();
  for (int i = 0; i < 3; i++) {
    auto* metric = metrics->Add();
    metric->set_name(absl::StrFormat("metric_%d", i));
  }

  batch_streamer->send(std::move(metrics));
}

} // namespace
} // namespace MetricsService
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
