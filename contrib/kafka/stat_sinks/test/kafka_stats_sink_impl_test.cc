#include "envoy/service/metrics/v3/metrics_service.pb.h"

#include "test/mocks/stats/mocks.h"

#include "contrib/kafka/filters/network/test/mesh/kafka_mocks.h"
#include "contrib/kafka/stat_sinks/source/kafka_stats_sink_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace std::chrono_literals;
using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {
namespace {

// ---------------------------------------------------------------------------
// Base test fixture with shared helpers
// ---------------------------------------------------------------------------

class KafkaMetricsFlusherTestBase : public testing::Test {
protected:
  void setupCounterAndGauge() {
    counter_.name_ = "test.counter";
    counter_.setTagExtractedName("test.counter");
    counter_.used_ = true;
    counter_.value_ = 10;
    counter_.latch_ = 5;
    counter_.setTags({{"env", "prod"}, {"region", "us-east"}});

    gauge_.name_ = "test.gauge";
    gauge_.setTagExtractedName("test.gauge");
    gauge_.used_ = true;
    gauge_.value_ = 42;
    gauge_.setTags({{"env", "prod"}});

    counters_.push_back({5, counter_});
    gauges_.push_back(gauge_);
  }

  void addHistogram() {
    histogram_ = std::make_unique<NiceMock<Stats::MockParentHistogram>>();
    histogram_->name_ = "test.histogram";
    histogram_->setTagExtractedName("test.histogram");
    histogram_->used_ = true;
    histogram_->setTags({{"env", "prod"}});
    histograms_.push_back(*histogram_);
  }

  void wireSnapshot() {
    ON_CALL(snapshot_, counters()).WillByDefault(ReturnRef(counters_));
    ON_CALL(snapshot_, gauges()).WillByDefault(ReturnRef(gauges_));
    ON_CALL(snapshot_, histograms()).WillByDefault(ReturnRef(histograms_));
    ON_CALL(snapshot_, snapshotTime())
        .WillByDefault(Return(SystemTime(std::chrono::milliseconds(1000))));
  }

  NiceMock<Stats::MockCounter> counter_;
  NiceMock<Stats::MockGauge> gauge_;
  std::unique_ptr<NiceMock<Stats::MockParentHistogram>> histogram_;
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
  std::vector<Stats::MetricSnapshot::CounterSnapshot> counters_;
  std::vector<std::reference_wrapper<const Stats::Gauge>> gauges_;
  std::vector<std::reference_wrapper<const Stats::ParentHistogram>> histograms_;
};

// ---------------------------------------------------------------------------
// JSON serialization tests
// ---------------------------------------------------------------------------

class JsonFlusherTest : public KafkaMetricsFlusherTestBase {
protected:
  KafkaMetricsFlusher flusher_{true, true, SerializationFormat::Json};
};

TEST_F(JsonFlusherTest, FlushCountersAndGauges) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  const auto& msg = messages[0];
  EXPECT_NE(msg.find("\"type\":\"counter\""), std::string::npos);
  EXPECT_NE(msg.find("\"name\":\"test.counter\""), std::string::npos);
  EXPECT_NE(msg.find("\"value\":5"), std::string::npos);
  EXPECT_NE(msg.find("\"delta\":true"), std::string::npos);
  EXPECT_NE(msg.find("\"type\":\"gauge\""), std::string::npos);
  EXPECT_NE(msg.find("\"name\":\"test.gauge\""), std::string::npos);
  EXPECT_NE(msg.find("\"value\":42"), std::string::npos);
  EXPECT_NE(msg.find("\"timestamp_ms\":1000"), std::string::npos);
}

TEST_F(JsonFlusherTest, FlushWithBatching) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 1);
  ASSERT_EQ(2, messages.size());

  EXPECT_NE(messages[0].find("\"type\":\"counter\""), std::string::npos);
  EXPECT_NE(messages[1].find("\"type\":\"gauge\""), std::string::npos);
}

TEST_F(JsonFlusherTest, FlushEmptySnapshot) {
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(messages[0], "{\"metrics\":[]}");
}

TEST_F(JsonFlusherTest, AbsoluteCounterValues) {
  KafkaMetricsFlusher abs_flusher(false, true, SerializationFormat::Json);

  counter_.name_ = "test.counter";
  counter_.used_ = true;
  counter_.value_ = 10;
  counters_.push_back({5, counter_});
  wireSnapshot();

  auto messages = abs_flusher.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());
  EXPECT_NE(messages[0].find("\"value\":10"), std::string::npos);
  EXPECT_EQ(messages[0].find("\"delta\""), std::string::npos);
}

TEST_F(JsonFlusherTest, TagsEmitted) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  EXPECT_NE(messages[0].find("\"tags\":{"), std::string::npos);
  EXPECT_NE(messages[0].find("\"env\":\"prod\""), std::string::npos);
  EXPECT_NE(messages[0].find("\"region\":\"us-east\""), std::string::npos);
}

TEST_F(JsonFlusherTest, TagsDisabled) {
  KafkaMetricsFlusher no_tags_flusher(true, false, SerializationFormat::Json);

  counter_.name_ = "test.counter.env.prod";
  counter_.used_ = true;
  counter_.value_ = 10;
  counter_.setTags({{"env", "prod"}});
  counters_.push_back({5, counter_});
  wireSnapshot();

  auto messages = no_tags_flusher.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());
  EXPECT_EQ(messages[0].find("\"tags\""), std::string::npos);
  EXPECT_NE(messages[0].find("\"name\":\"test.counter.env.prod\""), std::string::npos);
}

TEST_F(JsonFlusherTest, FlushHistogram) {
  addHistogram();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  const auto& msg = messages[0];
  EXPECT_NE(msg.find("\"type\":\"histogram\""), std::string::npos);
  EXPECT_NE(msg.find("\"name\":\"test.histogram\""), std::string::npos);
  EXPECT_NE(msg.find("\"sample_count\":"), std::string::npos);
  EXPECT_NE(msg.find("\"sample_sum\":"), std::string::npos);
  EXPECT_NE(msg.find("\"buckets\":["), std::string::npos);
  EXPECT_NE(msg.find("\"quantiles\":["), std::string::npos);
  EXPECT_NE(msg.find("\"timestamp_ms\":1000"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Protobuf serialization tests
// ---------------------------------------------------------------------------

class ProtobufFlusherTest : public KafkaMetricsFlusherTestBase {
protected:
  KafkaMetricsFlusher flusher_{true, true, SerializationFormat::Protobuf};

  envoy::service::metrics::v3::StreamMetricsMessage parseMessage(const std::string& data) {
    envoy::service::metrics::v3::StreamMetricsMessage msg;
    EXPECT_TRUE(msg.ParseFromString(data));
    return msg;
  }
};

TEST_F(ProtobufFlusherTest, FlushCountersAndGauges) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  auto msg = parseMessage(messages[0]);
  ASSERT_EQ(2, msg.envoy_metrics_size());

  const auto& counter_family = msg.envoy_metrics(0);
  EXPECT_EQ("test.counter", counter_family.name());
  EXPECT_EQ(io::prometheus::client::MetricType::COUNTER, counter_family.type());
  ASSERT_EQ(1, counter_family.metric_size());
  EXPECT_EQ(5, counter_family.metric(0).counter().value());
  EXPECT_EQ(1000, counter_family.metric(0).timestamp_ms());
  EXPECT_EQ(2, counter_family.metric(0).label_size());

  const auto& gauge_family = msg.envoy_metrics(1);
  EXPECT_EQ("test.gauge", gauge_family.name());
  EXPECT_EQ(io::prometheus::client::MetricType::GAUGE, gauge_family.type());
  ASSERT_EQ(1, gauge_family.metric_size());
  EXPECT_EQ(42, gauge_family.metric(0).gauge().value());
}

TEST_F(ProtobufFlusherTest, AbsoluteCounterValues) {
  KafkaMetricsFlusher abs_flusher(false, true, SerializationFormat::Protobuf);

  counter_.name_ = "test.counter";
  counter_.setTagExtractedName("test.counter");
  counter_.used_ = true;
  counter_.value_ = 10;
  counters_.push_back({5, counter_});
  wireSnapshot();

  auto messages = abs_flusher.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  auto msg = parseMessage(messages[0]);
  ASSERT_EQ(1, msg.envoy_metrics_size());
  EXPECT_EQ(10, msg.envoy_metrics(0).metric(0).counter().value());
}

TEST_F(ProtobufFlusherTest, DeltaCounterValues) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  auto msg = parseMessage(messages[0]);
  EXPECT_EQ(5, msg.envoy_metrics(0).metric(0).counter().value());
}

TEST_F(ProtobufFlusherTest, FlushWithBatching) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 1);
  ASSERT_EQ(2, messages.size());

  auto msg0 = parseMessage(messages[0]);
  ASSERT_EQ(1, msg0.envoy_metrics_size());
  EXPECT_EQ(io::prometheus::client::MetricType::COUNTER, msg0.envoy_metrics(0).type());

  auto msg1 = parseMessage(messages[1]);
  ASSERT_EQ(1, msg1.envoy_metrics_size());
  EXPECT_EQ(io::prometheus::client::MetricType::GAUGE, msg1.envoy_metrics(0).type());
}

TEST_F(ProtobufFlusherTest, FlushEmptySnapshot) {
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  auto msg = parseMessage(messages[0]);
  EXPECT_EQ(0, msg.envoy_metrics_size());
}

TEST_F(ProtobufFlusherTest, LabelsEmitted) {
  setupCounterAndGauge();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  auto msg = parseMessage(messages[0]);

  const auto& counter_metric = msg.envoy_metrics(0).metric(0);
  ASSERT_EQ(2, counter_metric.label_size());

  bool found_env = false, found_region = false;
  for (const auto& label : counter_metric.label()) {
    if (label.name() == "env" && label.value() == "prod") {
      found_env = true;
    }
    if (label.name() == "region" && label.value() == "us-east") {
      found_region = true;
    }
  }
  EXPECT_TRUE(found_env);
  EXPECT_TRUE(found_region);
}

TEST_F(ProtobufFlusherTest, LabelsDisabled) {
  KafkaMetricsFlusher no_labels_flusher(true, false, SerializationFormat::Protobuf);

  counter_.name_ = "test.counter.env.prod";
  counter_.setTagExtractedName("test.counter");
  counter_.used_ = true;
  counter_.value_ = 10;
  counter_.setTags({{"env", "prod"}});
  counters_.push_back({5, counter_});
  wireSnapshot();

  auto messages = no_labels_flusher.flush(snapshot_, 0);
  auto msg = parseMessage(messages[0]);
  ASSERT_EQ(1, msg.envoy_metrics_size());
  EXPECT_EQ("test.counter.env.prod", msg.envoy_metrics(0).name());
  EXPECT_EQ(0, msg.envoy_metrics(0).metric(0).label_size());
}

TEST_F(ProtobufFlusherTest, FlushHistogram) {
  addHistogram();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 0);
  ASSERT_EQ(1, messages.size());

  auto msg = parseMessage(messages[0]);
  ASSERT_EQ(1, msg.envoy_metrics_size());

  const auto& family = msg.envoy_metrics(0);
  EXPECT_EQ("test.histogram", family.name());
  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, family.type());
  ASSERT_EQ(1, family.metric_size());
  EXPECT_TRUE(family.metric(0).has_histogram());
  EXPECT_EQ(1000, family.metric(0).timestamp_ms());
}

TEST_F(ProtobufFlusherTest, FlushMixedMetricsWithBatching) {
  setupCounterAndGauge();
  addHistogram();
  wireSnapshot();

  auto messages = flusher_.flush(snapshot_, 2);
  ASSERT_EQ(2, messages.size());

  auto msg0 = parseMessage(messages[0]);
  EXPECT_EQ(2, msg0.envoy_metrics_size());

  auto msg1 = parseMessage(messages[1]);
  EXPECT_EQ(1, msg1.envoy_metrics_size());
  EXPECT_EQ(io::prometheus::client::MetricType::HISTOGRAM, msg1.envoy_metrics(0).type());
}

// ---------------------------------------------------------------------------
// KafkaStatsSink tests
// ---------------------------------------------------------------------------

class KafkaStatsSinkTest : public KafkaMetricsFlusherTestBase {};

TEST_F(KafkaStatsSinkTest, ProduceCalledOnFlush) {
  setupCounterAndGauge();
  wireSnapshot();

  auto mock_producer = std::make_unique<NiceMock<NetworkFilters::Kafka::Mesh::MockKafkaProducer>>();
  auto* producer_ptr = mock_producer.get();

  EXPECT_CALL(*producer_ptr, produce(std::string("test-topic"), _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(RdKafka::ERR_NO_ERROR));
  EXPECT_CALL(*producer_ptr, poll(0));

  KafkaMetricsFlusher flusher(true, true);
  KafkaStatsSink sink(std::move(mock_producer), "test-topic", 0, std::move(flusher));
  sink.flush(snapshot_);
}

TEST_F(KafkaStatsSinkTest, ProduceFailureLogged) {
  setupCounterAndGauge();
  wireSnapshot();

  auto mock_producer = std::make_unique<NiceMock<NetworkFilters::Kafka::Mesh::MockKafkaProducer>>();
  auto* producer_ptr = mock_producer.get();

  EXPECT_CALL(*producer_ptr, produce(std::string("test-topic"), _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(RdKafka::ERR__QUEUE_FULL))
      .WillOnce(Return(RdKafka::ERR_NO_ERROR));
  EXPECT_CALL(*producer_ptr, poll(_)).Times(testing::AtLeast(1));

  KafkaStatsSink sink(std::move(mock_producer), "test-topic", 0, KafkaMetricsFlusher(true, true));
  sink.flush(snapshot_);
}

TEST_F(KafkaStatsSinkTest, BatchingProducesMultipleMessages) {
  setupCounterAndGauge();
  wireSnapshot();

  auto mock_producer = std::make_unique<NiceMock<NetworkFilters::Kafka::Mesh::MockKafkaProducer>>();
  auto* producer_ptr = mock_producer.get();

  EXPECT_CALL(*producer_ptr, produce(std::string("test-topic"), _, _, _, _, _, _, _, _, _))
      .Times(2)
      .WillRepeatedly(Return(RdKafka::ERR_NO_ERROR));
  EXPECT_CALL(*producer_ptr, poll(0));

  KafkaMetricsFlusher flusher(true, true);
  KafkaStatsSink sink(std::move(mock_producer), "test-topic", 1, std::move(flusher));
  sink.flush(snapshot_);
}

TEST_F(KafkaStatsSinkTest, ProtobufFormatProducesToKafka) {
  setupCounterAndGauge();
  wireSnapshot();

  auto mock_producer = std::make_unique<NiceMock<NetworkFilters::Kafka::Mesh::MockKafkaProducer>>();
  auto* producer_ptr = mock_producer.get();

  EXPECT_CALL(*producer_ptr, produce(std::string("test-topic"), _, _, _, _, _, _, _, _, _))
      .WillOnce(Return(RdKafka::ERR_NO_ERROR));
  EXPECT_CALL(*producer_ptr, poll(0));

  KafkaMetricsFlusher flusher(true, true, SerializationFormat::Protobuf);
  KafkaStatsSink sink(std::move(mock_producer), "test-topic", 0, std::move(flusher));
  sink.flush(snapshot_);
}

} // namespace
} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
