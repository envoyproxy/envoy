#include "extensions/filters/network/kafka/broker/filter.h"

#include <sstream>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "common/common/assert.h"
#include "common/common/utility.h"

#include "extensions/filters/network/kafka/broker/filter_impl.h"
#include "extensions/filters/network/kafka/external/request_metrics.h"
#include "extensions/filters/network/kafka/external/response_metrics.h"
#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

KafkaBrokerFilter::KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source,
                                     const std::string& stat_prefix)
    : KafkaBrokerFilter{
          std::make_shared<MetricTrackingCallback>(scope, time_source, stat_prefix)} {};

KafkaBrokerFilter::KafkaBrokerFilter(const KafkaCallbackSharedPtr& metrics_callback)
    : response_decoder_{new ResponseDecoder({metrics_callback})},
      request_decoder_{new RequestDecoder(
          {std::make_shared<Forwarder>(*response_decoder_), metrics_callback})} {};

KafkaBrokerFilter::KafkaBrokerFilter(ResponseDecoderSharedPtr response_decoder,
                                     RequestDecoderSharedPtr request_decoder)
    : response_decoder_{response_decoder}, request_decoder_{request_decoder} {};

Network::FilterStatus KafkaBrokerFilter::onNewConnection() {
  return Network::FilterStatus::Continue;
}

void KafkaBrokerFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) {}

Network::FilterStatus KafkaBrokerFilter::onData(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka client [{} request bytes]", data.length());
  request_decoder_->onData(data);
  return Network::FilterStatus::Continue;
}

Network::FilterStatus KafkaBrokerFilter::onWrite(Buffer::Instance& data, bool) {
  ENVOY_LOG(trace, "data from Kafka broker [{} response bytes]", data.length());
  response_decoder_->onData(data);
  return Network::FilterStatus::Continue;
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
