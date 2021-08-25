#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_client_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

// Just a placeholder implementation.

PlaceholderKafkaProducer::PlaceholderKafkaProducer(Event::Dispatcher&, Thread::ThreadFactory&,
                                                   const RawKafkaProducerConfig&){};

void PlaceholderKafkaProducer::send(const ProduceFinishCbSharedPtr, const std::string&,
                                    const int32_t, const absl::string_view,
                                    const absl::string_view){};

void PlaceholderKafkaProducer::markFinished(){};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
