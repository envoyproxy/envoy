#pragma once

#include "envoy/common/time.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/kafka/external/requests.h"
#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"
#include "source/extensions/filters/network/kafka/mesh/request_processor.h"
#include "source/extensions/filters/network/kafka/request_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {
/**
 * Main entry point.
 * Decoded request bytes are passed to processor, that calls us back with enriched request.
 * Request then gets invoked with upstream Kafka facade, which will (in future) maintain
 * thread-local list of (enriched) Kafka producers. Filter is going to maintain a list of
 * in-flight-request so it can send responses when they finish.
 *
 *
 * +----------------+    <creates>    +-----------------------+
 * |RequestProcessor+----------------->AbstractInFlightRequest|
 * +-------^--------+                 +------^----------------+
 *         |                                 |
 *         |                                 |
 * +-------+-------+   <in-flight-reference> |
 * |KafkaMeshFilter+-------------------------+
 * +-------+-------+
 **/
class KafkaMeshFilter : public Network::ReadFilter,
                        public Network::ConnectionCallbacks,
                        public AbstractRequestListener,
                        private Logger::Loggable<Logger::Id::kafka> {
public:
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

  std::list<InFlightRequestSharedPtr>& getRequestsInFlightForTest();

private:
  // Helper method invoked when connection gets dropped.
  // Request references are going to be stored in 2 places: this filter (request's origin) and in
  // UpstreamKafkaClient instances (to match pure-Kafka confirmations to the requests). Because
  // filter can be destroyed before confirmations from Kafka are received, we are just going to mark
  // related requests as abandoned, so they do not attempt to reference this filter anymore.
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
