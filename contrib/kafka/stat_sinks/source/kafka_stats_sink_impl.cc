#include "contrib/kafka/stat_sinks/source/kafka_stats_sink_impl.h"

#include <chrono>
#include <string>
#include <vector>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {

// KafkaMetricsFlusher

std::vector<std::string> KafkaMetricsFlusher::flush(Stats::MetricSnapshot& snapshot,
                                                    uint32_t batch_size) const {
  if (format_ == SerializationFormat::Protobuf) {
    return flushProtobuf(snapshot, batch_size);
  }
  return flushJson(snapshot, batch_size);
}

// --- JSON serialization ---

void KafkaMetricsFlusher::addMetricCommonFields(Json::StringStreamer::Map& map,
                                                const Stats::Metric& metric,
                                                int64_t snapshot_time_ms) const {
  if (emit_tags_as_labels_) {
    map.addKey("name");
    map.addString(metric.tagExtractedName());

    const auto& tags = metric.tags();
    if (!tags.empty()) {
      map.addKey("tags");
      auto tags_map = map.addMap();
      for (const auto& tag : tags) {
        tags_map->addKey(tag.name_);
        tags_map->addString(tag.value_);
      }
    }
  } else {
    map.addKey("name");
    map.addString(metric.name());
  }

  map.addKey("timestamp_ms");
  map.addNumber(snapshot_time_ms);
}

namespace {

constexpr size_t kInitialJsonBatchCapacity = 4096;

struct JsonBatch {
  std::string output;
  std::unique_ptr<Json::StringStreamer> streamer;
  Json::StringStreamer::MapPtr root_map;
  Json::StringStreamer::ArrayPtr metrics_array;

  JsonBatch() {
    output.reserve(kInitialJsonBatchCapacity);
    streamer = std::make_unique<Json::StringStreamer>(output);
    root_map = streamer->makeRootMap();
    root_map->addKey("metrics");
    metrics_array = root_map->addArray();
  }

  std::string finalize() {
    metrics_array.reset();
    root_map.reset();
    streamer.reset();
    return std::move(output);
  }
};

} // namespace

std::vector<std::string> KafkaMetricsFlusher::flushJson(Stats::MetricSnapshot& snapshot,
                                                        uint32_t batch_size) const {
  std::vector<std::string> messages;
  int64_t snapshot_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 snapshot.snapshotTime().time_since_epoch())
                                 .count();

  uint32_t metric_count = 0;
  auto batch = std::make_unique<JsonBatch>();

  auto maybe_flush_batch = [&]() {
    if (batch_size > 0 && metric_count >= batch_size) {
      messages.push_back(batch->finalize());
      batch = std::make_unique<JsonBatch>();
      metric_count = 0;
    }
  };

  for (const auto& counter : snapshot.counters()) {
    if (counter.counter_.get().used()) {
      auto entry = batch->metrics_array->addMap();
      entry->addKey("type");
      entry->addString("counter");
      addMetricCommonFields(*entry, counter.counter_.get(), snapshot_time_ms);
      if (report_counters_as_deltas_) {
        entry->addKey("value");
        entry->addNumber(counter.delta_);
        entry->addKey("delta");
        entry->addBool(true);
      } else {
        entry->addKey("value");
        entry->addNumber(counter.counter_.get().value());
      }
      entry.reset();
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& gauge : snapshot.gauges()) {
    if (gauge.get().used()) {
      auto entry = batch->metrics_array->addMap();
      entry->addKey("type");
      entry->addString("gauge");
      addMetricCommonFields(*entry, gauge.get(), snapshot_time_ms);
      entry->addKey("value");
      entry->addNumber(gauge.get().value());
      entry.reset();
      metric_count++;
      maybe_flush_batch();
    }
  }

  for (const auto& histogram : snapshot.histograms()) {
    if (histogram.get().used()) {
      const Stats::HistogramStatistics& hist_stats = histogram.get().intervalStatistics();
      auto entry = batch->metrics_array->addMap();
      entry->addKey("type");
      entry->addString("histogram");
      addMetricCommonFields(*entry, histogram.get(), snapshot_time_ms);

      entry->addKey("sample_count");
      entry->addNumber(hist_stats.sampleCount());
      entry->addKey("sample_sum");
      entry->addNumber(hist_stats.sampleSum());

      {
        entry->addKey("buckets");
        auto buckets_arr = entry->addArray();
        for (size_t i = 0; i < hist_stats.supportedBuckets().size(); i++) {
          auto bucket = buckets_arr->addMap();
          bucket->addKey("upper_bound");
          bucket->addNumber(hist_stats.supportedBuckets()[i]);
          bucket->addKey("cumulative_count");
          bucket->addNumber(hist_stats.computedBuckets()[i]);
        }
      }

      {
        entry->addKey("quantiles");
        auto quantiles_arr = entry->addArray();
        for (size_t i = 0; i < hist_stats.supportedQuantiles().size(); i++) {
          auto quantile = quantiles_arr->addMap();
          quantile->addKey("quantile");
          quantile->addNumber(hist_stats.supportedQuantiles()[i]);
          quantile->addKey("value");
          quantile->addNumber(hist_stats.computedQuantiles()[i]);
        }
      }

      entry.reset();
      metric_count++;
      maybe_flush_batch();
    }
  }

  if (metric_count > 0 || messages.empty()) {
    messages.push_back(batch->finalize());
  }

  return messages;
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
      populateMetricsFamily(*family, io::prometheus::client::MetricType::COUNTER, snapshot_time_ms,
                            counter.counter_.get());
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
  constexpr int kMaxQueueFullRetries = 3;
  RdKafka::ErrorCode ec = RdKafka::ERR_NO_ERROR;

  for (int attempt = 0; attempt < kMaxQueueFullRetries; ++attempt) {
    // const_cast is safe: RK_MSG_COPY makes librdkafka copy the data before returning.
    ec = producer_->produce(topic_, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                            const_cast<char*>(message.data()), message.size(), nullptr, 0, 0,
                            nullptr, nullptr);
    if (ec == RdKafka::ERR_NO_ERROR) {
      return;
    }
    if (ec != RdKafka::ERR__QUEUE_FULL) {
      ENVOY_LOG(warn, "Failed to produce metrics to Kafka topic '{}': {}", topic_,
                RdKafka::err2str(ec));
      return;
    }
    // Poll to run delivery callbacks and free queue space before retrying.
    producer_->poll(100);
  }

  ENVOY_LOG(warn,
            "Dropping metrics batch for Kafka topic '{}': producer queue full after {} retries",
            topic_, kMaxQueueFullRetries);
}

} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
