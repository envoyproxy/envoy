#pragma once

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"
#include "contrib/kafka/filters/network/source/mesh/request_processor.h"
#include "contrib/kafka/filters/network/source/mesh/shared_consumer_manager.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_config.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_kafka_facade.h"
#include "contrib/kafka/filters/network/source/request_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Main entry point.
 * Decoded request bytes are passed to processor, that calls us back with enriched request.
 * Request then gets invoked to starts its processing.
 * Filter is going to maintain a list of in-flight-request so it can send responses when they
 * finish.
 *
 * See command_handlers.md for particular request interactions.
 **/
class KafkaMeshFilter : public Network::ReadFilter,
                        public Network::ConnectionCallbacks,
                        public AbstractRequestListener,
                        private Logger::Loggable<Logger::Id::kafka> {
public:
  // Main constructor.
  KafkaMeshFilter(const UpstreamKafkaConfiguration& configuration,
                  UpstreamKafkaFacade& upstream_kafka_facade,
                  RecordCallbackProcessor& record_callback_processor);

  // Visible for testing.
  KafkaMeshFilter(RequestDecoderSharedPtr request_decoder);

  // Non-trivial. See 'abandonAllInFlightRequests'.
  ~KafkaMeshFilter() override;

  // Network::ReadFilter
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

  // AbstractRequestListener
  void onRequest(InFlightRequestSharedPtr request) override;
  void onRequestReadyForAnswer() override;
  Event::Dispatcher& dispatcher() override;

  std::list<InFlightRequestSharedPtr>& getRequestsInFlightForTest();

private:
  // Helper method invoked when connection gets dropped.
  // Because filter can be destroyed before confirmations from Kafka are received, we are just going
  // to mark related requests as abandoned, so they do not attempt to reference this filter anymore.
  // Impl note: this is similar to what Redis filter does.
  void abandonAllInFlightRequests();

  const RequestDecoderSharedPtr request_decoder_;

  Network::ReadFilterCallbacks* read_filter_callbacks_;

  std::list<InFlightRequestSharedPtr> requests_in_flight_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
