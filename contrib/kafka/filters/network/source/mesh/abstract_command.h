#pragma once

#include "envoy/event/dispatcher.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/kafka_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Represents single downstream client request.
 * Responsible for performing the work on multiple upstream clusters and aggregating the results.
 */
class InFlightRequest {
public:
  virtual ~InFlightRequest() = default;

  /**
   * Begins processing of given request with context provided.
   */
  virtual void startProcessing() PURE;

  /**
   * Whether the given request has finished processing.
   * E.g. produce requests need to be forwarded upstream and get a response from Kafka cluster for
   * this to be true.
   */
  virtual bool finished() const PURE;

  /**
   * Creates a Kafka answer object that can be sent downstream.
   */
  virtual AbstractResponseSharedPtr computeAnswer() const PURE;

  /**
   * Abandon this request.
   * In-flight requests that have been abandoned are not going to cause any action after they have
   * finished processing.
   */
  virtual void abandon() PURE;
};

using InFlightRequestSharedPtr = std::shared_ptr<InFlightRequest>;

/**
 * Callback to be implemented by entities that are interested when the request has finished and has
 * answer ready.
 */
// Impl note: Filter implements this interface to keep track of requests coming to it.
class AbstractRequestListener {
public:
  virtual ~AbstractRequestListener() = default;

  // Notifies the listener that a new request has been received.
  virtual void onRequest(InFlightRequestSharedPtr request) PURE;

  // Notifies the listener, that the request finally has an answer ready.
  // Usually this means that the request has been sent to upstream Kafka clusters and we got answers
  // (unless it's something that could be responded to locally).
  // IMPL: we do not need to pass request here, as filters need to answer in-order.
  // What means that we always need to check if first answer is ready, even if the latter are
  // already finished.
  virtual void onRequestReadyForAnswer() PURE;

  // Accesses listener's dispatcher.
  // Used by non-Envoy threads that need to communicate with listeners.
  virtual Event::Dispatcher& dispatcher() PURE;
};

/**
 * Helper base class for all in flight requests.
 * Binds request to its origin filter.
 * All the fields can be accessed only by the owning dispatcher thread.
 */
class BaseInFlightRequest : public InFlightRequest, protected Logger::Loggable<Logger::Id::kafka> {
public:
  BaseInFlightRequest(AbstractRequestListener& filter) : filter_{filter} {};
  void abandon() override;

protected:
  /**
   * Notify the originating filter that this request has an answer ready.
   * This method is to be invoked by each request after it has finished processing.
   * Obviously, if the filter is no longer active (connection got closed before we were ready to
   * answer) nothing will happen.
   */
  void notifyFilter();

  // Filter that originated this request.
  AbstractRequestListener& filter_;

  // Whether the filter_ reference is still viable.
  bool filter_active_ = true;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
