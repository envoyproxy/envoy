#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/service/metrics/v3/metrics_service.pb.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/sink.h"

#include "source/common/common/logger.h"
#include "source/common/json/json_streamer.h"

#include "io/prometheus/client/metrics.pb.h"
#include "librdkafka/rdkafkacpp.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {

enum class SerializationFormat { Json, Protobuf };

class KafkaMetricsFlusher {
public:
  KafkaMetricsFlusher(bool report_counters_as_deltas, bool emit_tags_as_labels,
                      SerializationFormat format = SerializationFormat::Json)
      : report_counters_as_deltas_(report_counters_as_deltas),
        emit_tags_as_labels_(emit_tags_as_labels), format_(format) {}

  std::vector<std::string> flush(Stats::MetricSnapshot& snapshot, uint32_t batch_size) const;

private:
  std::vector<std::string> flushJson(Stats::MetricSnapshot& snapshot, uint32_t batch_size) const;
  std::vector<std::string> flushProtobuf(Stats::MetricSnapshot& snapshot,
                                         uint32_t batch_size) const;

  void addMetricCommonFields(Json::StringStreamer::Map& map, const Stats::Metric& metric,
                             int64_t snapshot_time_ms) const;

  void populateMetricsFamily(io::prometheus::client::MetricFamily& family,
                             io::prometheus::client::MetricType type, int64_t snapshot_time_ms,
                             const Stats::Metric& metric) const;

  const bool report_counters_as_deltas_{};
  const bool emit_tags_as_labels_{};
  const SerializationFormat format_{};
};

class KafkaStatsSink : public Stats::Sink, private Logger::Loggable<Logger::Id::kafka> {
public:
  KafkaStatsSink(std::unique_ptr<RdKafka::Producer> producer, const std::string& topic,
                 uint32_t batch_size, KafkaMetricsFlusher flusher);

  ~KafkaStatsSink() override;

  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  void produce(const std::string& message);

  std::unique_ptr<RdKafka::Producer> producer_;
  const std::string topic_;
  const uint32_t batch_size_{};
  const KafkaMetricsFlusher flusher_;
};

} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
