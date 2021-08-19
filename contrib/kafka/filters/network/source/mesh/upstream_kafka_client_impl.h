#pragma once

#include "envoy/event/dispatcher.h"

#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using RawKafkaProducerConfig = std::map<std::string, std::string>;

// Placeholder for proper Kafka Producer object.
// It will also keep a reference to a dedicated thread (that's why we need a factory) that's going
// to be polling for delivery notifications.
class PlaceholderKafkaProducer : public KafkaProducer {
public:
  PlaceholderKafkaProducer(Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory,
                           const RawKafkaProducerConfig& configuration);

  // KafkaProducer
  void send(const ProduceFinishCbSharedPtr origin, const std::string& topic,
            const int32_t partition, const absl::string_view key,
            const absl::string_view value) override;

  // KafkaProducer
  void markFinished() override;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
