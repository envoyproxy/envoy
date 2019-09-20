#pragma once

#include "envoy/common/time.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/parser.h"
#include "extensions/filters/network/kafka/request_codec.h"
#include "extensions/filters/network/kafka/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

/**
 * Utility class that merges both request & response callbacks.
 */
class KafkaCallback : public RequestCallback, public ResponseCallback {};

using KafkaCallbackSharedPtr = std::shared_ptr<KafkaCallback>;

/**
 * Implementation of Kafka broker-level filter.
 */
class KafkaBrokerFilter : public Network::Filter, private Logger::Loggable<Logger::Id::kafka> {

public:
  // Main constructor.
  KafkaBrokerFilter(Stats::Scope& scope, TimeSource& time_source, const std::string& stat_prefix);

  // Visible for testing only.
  // Allows for injecting request and response decoders.
  KafkaBrokerFilter(ResponseDecoderSharedPtr response_decoder,
                    RequestDecoderSharedPtr request_decoder);

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  // Network::WriteFilter
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;

  ResponseDecoderSharedPtr getResponseDecoderForTest();

private:
  KafkaBrokerFilter(const KafkaCallbackSharedPtr& callback);

  ResponseDecoderSharedPtr response_decoder_;
  RequestDecoderSharedPtr request_decoder_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
