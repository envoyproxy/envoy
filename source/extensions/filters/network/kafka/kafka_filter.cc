#include "extensions/filters/network/kafka/kafka_filter.h"

#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

class LoggingMessageListener : public MessageListener, public Logger::Loggable<Logger::Id::kafka> {
public:
  void onMessage(MessageSharedPtr arg) override { ENVOY_LOG(info, "received: {}", *arg); }
};

KafkaFilter::KafkaFilter(Stats::Scope& scope, std::shared_ptr<MetricsHolder> metrics_holder)
    : stats_{KAFKA_STATS(POOL_COUNTER_PREFIX(scope, "kafka."))}, metrics_holder_{metrics_holder},
      request_decoder_{RequestParserResolver::KAFKA_0_11,
                       {std::make_unique<LoggingMessageListener>()}} {
  stats_.filters_created_.inc();
};

Network::FilterStatus KafkaFilter::onNewConnection() { return Network::FilterStatus::Continue; }

void KafkaFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
}

Network::FilterStatus KafkaFilter::onData(Buffer::Instance& data, bool) {
  request_decoder_.onData(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus KafkaFilter::onWrite(Buffer::Instance& data, bool) {
  response_decoder_.onWrite(data);
  return Network::FilterStatus::Continue;
}

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
