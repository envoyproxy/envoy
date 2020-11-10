#include "extensions/filters/network/kafka/mesh/filter.h"

#include <thread>

#include "envoy/network/connection.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/external/responses.h"
#include "extensions/filters/network/kafka/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

KafkaMeshFilter::KafkaMeshFilter(const ClusteringConfiguration& clustering_configuration,
                                 UpstreamKafkaFacade& upstream_kafka_facade)
    : KafkaMeshFilter{clustering_configuration, upstream_kafka_facade,
                      std::make_shared<RequestDecoder>(std::vector<RequestCallbackSharedPtr>(
                          {std::make_shared<RequestProcessor>(*this, clustering_configuration)}))} {
}

KafkaMeshFilter::KafkaMeshFilter(const ClusteringConfiguration&,
                                 UpstreamKafkaFacade& upstream_kafka_facade,
                                 RequestDecoderSharedPtr request_decoder)
    : request_decoder_{request_decoder}, upstream_kafka_facade_{upstream_kafka_facade} {}

KafkaMeshFilter::~KafkaMeshFilter() { abandonAllInFlightRequests(); }

Network::FilterStatus KafkaMeshFilter::onNewConnection() {
  ENVOY_LOG(trace, "KafkaMeshFilter - onNewConnection");
  return Network::FilterStatus::Continue;
}

void KafkaMeshFilter::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_filter_callbacks_ = &callbacks;
  read_filter_callbacks_->connection().addConnectionCallbacks(*this);
}

Network::FilterStatus KafkaMeshFilter::onData(Buffer::Instance& data, bool) {
  const std::thread::id tid = std::this_thread::get_id();
  ENVOY_LOG(trace, "KafkaMeshFilter - onData [{} request bytes] IN {}", data.length(), tid);

  try {
    request_decoder_->onData(data);
    data.drain(data.length()); // All the bytes have been copied to decoder.
    return Network::FilterStatus::StopIteration;
  } catch (const EnvoyException& e) {
    ENVOY_LOG(info, "could not process data from Kafka client: {}", e.what());
    request_decoder_->reset();
    // Something very wrong occurred, let's just close the connection.
    read_filter_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
    return Network::FilterStatus::StopIteration;
  }
}

void KafkaMeshFilter::onEvent(Network::ConnectionEvent event) {
  if (Network::ConnectionEvent::RemoteClose == event ||
      Network::ConnectionEvent::LocalClose == event) {
    abandonAllInFlightRequests();
  }
}

void KafkaMeshFilter::onAboveWriteBufferHighWatermark() {}

void KafkaMeshFilter::onBelowWriteBufferLowWatermark() {}

/**
 * We have received a request we can actually process.
 */
void KafkaMeshFilter::onRequest(InFlightRequestSharedPtr request) {
  requests_in_flight_.push_back(request);
  request->invoke(upstream_kafka_facade_);
}

/**
 * Our filter has been notified that a request that originated in this filter has an answer ready.
 * Because the Kafka messages have ordering, we need to check all messages and can possibly send
 * multiple answers in one go. This can happen if e.g. message 3 finishes first, then 2, then 1,
 * what allows us to send 1, 2, 3 in one invocation.
 */
void KafkaMeshFilter::onRequestReadyForAnswer() {
  while (!requests_in_flight_.empty()) {
    InFlightRequestSharedPtr rq = requests_in_flight_.front();
    if (rq->finished()) {
      // The request has been finished, so we no longer need to store it.
      requests_in_flight_.erase(requests_in_flight_.begin());

      // And write the response downstream.
      const AbstractResponseSharedPtr response = rq->computeAnswer();
      Buffer::OwnedImpl buffer;
      ResponseEncoder encoder{buffer};
      encoder.encode(*response);
      read_filter_callbacks_->connection().write(buffer, false);
    } else {
      break;
    }
  }
}

// Request references are stored in 2 places: this filter (request's origin) and in
// UpstreamKafkaClient instances (to match pure-Kafka confirmations to the requests). Because filter
// can die before confirmation from Kafka is received, we are just going to mark them as abandoned,
// so they do not attempt to reference this filter anymore.
void KafkaMeshFilter::abandonAllInFlightRequests() {
  for (const auto& request : requests_in_flight_) {
    request->abandon();
  }
  requests_in_flight_.erase(requests_in_flight_.begin(), requests_in_flight_.end());
}

std::list<InFlightRequestSharedPtr>& KafkaMeshFilter::getRequestsInFlightForTest() {
  return requests_in_flight_;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
