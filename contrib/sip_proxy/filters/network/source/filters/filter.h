#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "contrib/sip_proxy/filters/network/source/decoder_events.h"
#include "contrib/sip_proxy/filters/network/source/router/router.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"
#include "contrib/sip_proxy/filters/network/source/stats.h"
#include "contrib/sip_proxy/filters/network/source/tra/tra.h"
#include "contrib/sip_proxy/filters/network/source/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
class TrafficRoutingAssistantHandler;
namespace SipFilters {

enum class ResponseStatus {
  MoreData = 0, // The upstream response requires more data.
  Complete = 1, // The upstream response is complete.
  Reset = 2,    // The upstream response is invalid and its connection must be reset.
};

/**
 * Decoder filter callbacks add additional callbacks.
 */
class DecoderFilterCallbacks : public SipProxy::PendingListHandler {
public:
  ~DecoderFilterCallbacks() override = default;

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() const PURE;

  /**
   * @return string the ID of the transaction.
   */
  virtual std::string transactionId() const PURE;

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * @return RouteConstSharedPtr the route for the current request.
   */
  virtual Router::RouteConstSharedPtr route() PURE;

  virtual SipFilterStats& stats() PURE;

  /**
   * Create a locally generated response using the provided response object.
   * @param response DirectResponse the response to send to the downstream client
   * @param end_stream if true, the downstream connection should be closed after this response
   */
  virtual void sendLocalReply(const SipProxy::DirectResponse& response, bool end_stream) PURE;

  /**
   * Indicates the start of an upstream response. May only be called once.
   * @param transport the transport used by the upstream response
   * @param protocol the protocol used by the upstream response
   */
  virtual void startUpstreamResponse() PURE;

  /**
   * Called with upstream response data.
   * @param data supplies the upstream's data
   * @return ResponseStatus indicating if the upstream response requires more data, is complete,
   *         or if an error occurred requiring the upstream connection to be reset.
   */
  virtual ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Reset the downstream connection.
   */
  virtual void resetDownstreamConnection() PURE;

  /**
   * @return StreamInfo for logging purposes.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  virtual std::shared_ptr<Router::TransactionInfos> transactionInfos() PURE;
  virtual std::shared_ptr<SipProxy::SipSettings> settings() const PURE;
  virtual std::shared_ptr<SipProxy::TrafficRoutingAssistantHandler> traHandler() PURE;
  virtual void onReset() PURE;
  virtual void continueHandling(const std::string& key, bool try_next_affinity) PURE;
  virtual MessageMetadataSharedPtr metadata() PURE;
};

/**
 * Decoder filter interface.
 */
class DecoderFilter : public virtual DecoderEventHandler {
public:
  ~DecoderFilter() override = default;

  /**
   * This routine is called prior to a filter being destroyed. This may happen after normal stream
   * finish (both downstream and upstream) or due to reset. Every filter is responsible for making
   * sure that any async events are cleaned up in the context of this routine. This includes timers,
   * network calls, etc. The reason there is an onDestroy() method vs. doing this type of cleanup
   * in the destructor is due to the deferred deletion model that Envoy uses to avoid stack unwind
   * complications. Filters must not invoke either encoder or decoder filter callbacks after having
   * onDestroy() invoked.
   */
  virtual void onDestroy() PURE;

  /**
   * Called by the connection manager once to initialize the filter decoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) PURE;
};

using DecoderFilterSharedPtr = std::shared_ptr<DecoderFilter>;

/**
 * These callbacks are provided by the connection manager to the factory so that the factory can
 * build the filter chain in an application specific way.
 */
class FilterChainFactoryCallbacks {
public:
  virtual ~FilterChainFactoryCallbacks() = default;

  /**
   * Add a decoder filter that is used when reading connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addDecoderFilter(DecoderFilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a Sip filter chain for new connections as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * A FilterChainFactory is used by a connection manager to create a Sip level filter chain when
 * a new connection is created. Typically it would be implemented by a configuration engine that
 * would install a set of filters that are able to process an application scenario on top of a
 * stream of Sip requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new Sip stream is created on the connection.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) PURE;
};

} // namespace SipFilters
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
