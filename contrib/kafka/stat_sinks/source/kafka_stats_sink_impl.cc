#include "contrib/kafka/stat_sinks/source/kafka_stats_sink_impl.h"

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {

namespace {

void jsonEscape(std::string& output, const std::string& input) {
  for (char c : input) {
    switch (c) {
    case '"':
      output += "\\\"";
      break;
    case '\\':
      output += "\\\\";
      break;
    case '\n':
      output += "\\n";
      break;
    default:
      output += c;
    }
  }
}

} // namespace

// KafkaMetricsFlusher

std::vector<std::string> KafkaMetricsFlusher::flush(Stats::MetricSnapshot& snapshot,
                                                     uint32_t batch_size) const {
  if (format_ == SerializationFormat::Protobuf) {
    return flushProtobuf(snapshot, batch_size);
  }
  return flushJson(snapshot, batch_size);
}

// --- JSON serialization ---

std::vector<std::string> KafkaMetricsFlusher::flushJson(Stats::MetricSnapshot& snapshot,
                                                         uint32_t batch_size) const {
  std::vector<std::string> messages;
  int64_t snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();

  uint32_t metric_count = 0;
  std::string current_batch;
  current_batch.reserve(4096);
  current_batch += "{\"metrics\":[";
  bool first = true;

  auto maybe_flush_batch = [&]() {
    if (batch_size > 0 && metric_count >= batch_size) {
      current_batch += "]}";
      messages.push_back(std::move(current_batch));
      current_batch.clear();
      current_batch.reserve(4096);
      current_batch += "{\"metrics\":[";
      metric_count = 0;
      first = true;
    }
  };

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      flushCounterJson(current_batch, counter, snapshot_time_ms, first);
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      flushGaugeJson(current_batch, gauge.get(), snapshot_time_ms, first);
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (histogram.get().used()) {
      flushHistogramJson(current_batch, histogram.get(), snapshot_time_ms, first);
      metric_count++;
      maybe_flush_batch();
    }
  }

  if (metric_count > 0 || messages.empty()) {
    current_batch += "]}";
    messages.push_back(std::move(current_batch));
  }

  return messages;
}

void KafkaMetricsFlusher::flushCounterJson(
    std::string& output, const Stats::MetricSnapshot::CounterSnapshot& counter_snapshot,
    int64_t snapshot_time_ms, bool& first) const {
  if (!first) {
    output += ',';
  }
  first = false;

  output += "{\"type\":\"counter\"";
  appendMetricName(output, counter_snapshot.counter_.get());
  appendTags(output, counter_snapshot.counter_.get());

  if (report_counters_as_deltas_) {
    output += ",\"value\":";
    output += std::to_string(counter_snapshot.delta_);
    output += ",\"delta\":true";
  } else {
    output += ",\"value\":";
    output += std::to_string(counter_snapshot.counter_.get().value());
  }

  output += ",\"timestamp_ms\":";
  output += std::to_string(snapshot_time_ms);
  output += '}';
}

void KafkaMetricsFlusher::flushGaugeJson(std::string& output, const Stats::Gauge& gauge,
                                          int64_t snapshot_time_ms, bool& first) const {
  if (!first) {
    output += ',';
  }
  first = false;

  output += "{\"type\":\"gauge\"";
  appendMetricName(output, gauge);
  appendTags(output, gauge);

  output += ",\"value\":";
  output += std::to_string(gauge.value());
  output += ",\"timestamp_ms\":";
  output += std::to_string(snapshot_time_ms);
  output += '}';
}

void KafkaMetricsFlusher::flushHistogramJson(std::string& output,
                                              const Stats::ParentHistogram& histogram,
                                              int64_t snapshot_time_ms, bool& first) const {
  if (!first) {
    output += ',';
  }
  first = false;

  const Stats::HistogramStatistics& hist_stats = histogram.intervalStatistics();

  output += "{\"type\":\"histogram\"";
  appendMetricName(output, histogram);
  appendTags(output, histogram);

  output += ",\"sample_count\":";
  output += std::to_string(hist_stats.sampleCount());
  output += ",\"sample_sum\":";
  output += std::to_string(hist_stats.sampleSum());

  output += ",\"buckets\":[";
  bool first_bucket = true;
  for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
    if (!first_bucket) {
      output += ',';
    }
    first_bucket = false;
    output += "{\"upper_bound\":";
    output += std::to_string(hist_stats.supportedBuckets()[i]);
    output += ",\"cumulative_count\":";
    output += std::to_string(hist_stats.computedBuckets()[i]);
    output += '}';
  }
  output += ']';

  output += ",\"quantiles\":[";
  bool first_quantile = true;
  for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
    if (!first_quantile) {
      output += ',';
    }
    first_quantile = false;
    output += "{\"quantile\":";
    output += std::to_string(hist_stats.supportedQuantiles()[i]);
    output += ",\"value\":";
    output += std::to_string(hist_stats.computedQuantiles()[i]);
    output += '}';
  }
  output += ']';

  output += ",\"timestamp_ms\":";
  output += std::to_string(snapshot_time_ms);
  output += '}';
}

// --- Protobuf serialization ---

void KafkaMetricsFlusher::populateMetricsFamily(io::prometheus::client::MetricFamily& family,
                                                 io::prometheus::client::MetricType type,
                                                 int64_t snapshot_time_ms,
                                                 const Stats::Metric& metric) const {
  family.set_type(type);
  auto* prometheus_metric = family.add_metric();
  prometheus_metric->set_timestamp_ms(snapshot_time_ms);

  if (emit_tags_as_labels_) {
    family.set_name(metric.tagExtractedName());
    for (const auto& tag : metric.tags()) {
      auto* label = prometheus_metric->add_label();
      label->set_name(tag.name_);
      label->set_value(tag.value_);
    }
  } else {
    family.set_name(metric.name());
  }
}

std::vector<std::string> KafkaMetricsFlusher::flushProtobuf(Stats::MetricSnapshot& snapshot,
                                                             uint32_t batch_size) const {
  std::vector<std::string> messages;
  int64_t snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();

  envoy::service::metrics::v3::StreamMetricsMessage current_message;
  uint32_t metric_count = 0;

  auto maybe_flush_batch = [&]() {
    if (batch_size > 0 && metric_count >= batch_size) {
      messages.push_back(current_message.SerializeAsString());
      current_message.Clear();
      metric_count = 0;
    }
  };

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      auto* family = current_message.add_envoy_metrics();
      populateMetricsFamily(*family, io::prometheus::client::MetricType::COUNTER,
                            snapshot_time_ms, counter.counter_.get());
      auto* counter_metric = family->mutable_metric(0)->mutable_counter();
      if (report_counters_as_deltas_) {
        counter_metric->set_value(counter.delta_);
      } else {
        counter_metric->set_value(counter.counter_.get().value());
      }
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      auto* family = current_message.add_envoy_metrics();
      populateMetricsFamily(*family, io::prometheus::client::MetricType::GAUGE, snapshot_time_ms,
                            gauge.get());
      family->mutable_metric(0)->mutable_gauge()->set_value(gauge.get().value());
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (histogram.get().used()) {
      auto* family = current_message.add_envoy_metrics();
      populateMetricsFamily(*family, io::prometheus::client::MetricType::HISTOGRAM,
                            snapshot_time_ms, histogram.get());
      const Stats::HistogramStatistics& hist_stats = histogram.get().intervalStatistics();
      auto* proto_hist = family->mutable_metric(0)->mutable_histogram();
      proto_hist->set_sample_count(hist_stats.sampleCount());
      proto_hist->set_sample_sum(hist_stats.sampleSum());
      for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
        auto* bucket = proto_hist->add_bucket();
        bucket->set_upper_bound(hist_stats.supportedBuckets()[i]);
        bucket->set_cumulative_count(hist_stats.computedBuckets()[i]);
      }
      metric_count++;
      maybe_flush_batch();
    }
  }

  if (metric_count > 0 || messages.empty()) {
    messages.push_back(current_message.SerializeAsString());
  }

  return messages;
}

void KafkaMetricsFlusher::appendMetricName(std::string& output,
                                            const Stats::Metric& metric) const {
  output += ",\"name\":\"";
  if (emit_tags_as_labels_) {
    jsonEscape(output, metric.tagExtractedName());
  } else {
    jsonEscape(output, metric.name());
  }
  output += '"';
}

void KafkaMetricsFlusher::appendTags(std::string& output, const Stats::Metric& metric) const {
  if (!emit_tags_as_labels_) {
    return;
  }
  const auto& tags = metric.tags();
  if (tags.empty()) {
    return;
  }
  output += ",\"tags\":{";
  bool first_tag = true;
  for (const auto& tag : tags) {
    if (!first_tag) {
      output += ',';
    }
    first_tag = false;
    output += '"';
    jsonEscape(output, tag.name_);
    output += "\":\"";
    jsonEscape(output, tag.value_);
    output += '"';
  }
  output += '}';
}

// KafkaStatsSink

KafkaStatsSink::KafkaStatsSink(std::unique_ptr<RdKafka::Producer> producer,
                               const std::string& topic, uint32_t batch_size,
                               KafkaMetricsFlusher flusher)
    : producer_(std::move(producer)), topic_(topic), batch_size_(batch_size),
      flusher_(std::move(flusher)) {}

KafkaStatsSink::~KafkaStatsSink() {
  if (producer_) {
    producer_->flush(5000);
  }
}

void KafkaStatsSink::flush(Stats::MetricSnapshot& snapshot) {
  auto messages = flusher_.flush(snapshot, batch_size_);
  for (const auto& message : messages) {
    produce(message);
  }
  producer_->poll(0);
}

void KafkaStatsSink::produce(const std::string& message) {
  RdKafka::ErrorCode ec = producer_->produce(
      topic_, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
      const_cast<char*>(message.data()), message.size(), nullptr, 0, 0, nullptr, nullptr);

  if (ec != RdKafka::ERR_NO_ERROR) {
    ENVOY_LOG(warn, "Failed to produce metrics to Kafka topic '{}': {}", topic_,
              RdKafka::err2str(ec));
    if (ec == RdKafka::ERR__QUEUE_FULL) {
      producer_->poll(100);
    }
  }
}

} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
