#pragma once

#include "common/common/logger.h"

#include "extensions/filters/network/kafka/kafka_response.h"
#include "extensions/filters/network/kafka/mesh/clustering.h"
#include "extensions/filters/network/kafka/mesh/upstream_kafka_facade.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class AbstractRequestListener;

/**
 * Represents single downstream client request.
 * Responsible for performing the work on multiple upstream clusters and aggregating the results.
 */
class AbstractInFlightRequest : protected Logger::Loggable<Logger::Id::kafka> {
public:
  AbstractInFlightRequest(AbstractRequestListener& filter) : filter_{filter} {};

  virtual ~AbstractInFlightRequest() = default;

  virtual void invoke(UpstreamKafkaFacade&) PURE;

  virtual bool finished() const PURE;

  virtual AbstractResponseSharedPtr computeAnswer() const PURE;

  /**
   * Abandon this request.
   * In-flight requests that have been abandoned are not going to cause any action after they have
   * finished processing.
   */
  void abandon();

protected:
  /**
   * Notify the originating filter that this request has an answer ready.
   */
  void notifyFilter();

  // Filter that originated this request.
  AbstractRequestListener& filter_;

  // Whether the filter_ reference is still alive.
  bool filter_active_ = true;
};

using AbstractInFlightRequestSharedPtr = std::shared_ptr<AbstractInFlightRequest>;

/**
 * Callback to be implemented by entities that are interested when the request has finished and has
 * answer ready.
 */
// Impl note: Filter implements this interface to keep track of requests coming to it.
class AbstractRequestListener {
public:
  virtual ~AbstractRequestListener() = default;

  // Notifies the listener that a new request has been received.
  virtual void onRequest(AbstractInFlightRequestSharedPtr request) PURE;

  // Notified the listener, that the request finally has an answer ready.
  // Usually this means that the request has been sent to upstream Kafka clusters and we got answers
  // (unless it's something that could be responded to locally).
  // IMPL: we do not need to pass request here, as filters need to answer in-order.
  // What means that we always need to check if first answer is ready, even if the latter are
  // already finished.
  virtual void onRequestReadyForAnswer() PURE;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
