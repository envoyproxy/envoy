#pragma once

#include <list>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

/**
 * Decoder filter callbacks add additional callbacks.
 */
class DecoderFilterCallbacks {
public:
  virtual ~DecoderFilterCallbacks() {}

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() const PURE;

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * Continue iterating through the filter chain with buffered data. This routine can only be
   * called if the filter has previously returned StopIteration from one of the DecoderFilter
   * methods. The connection manager will callbacks to the next filter in the chain. Further note
   * that if the request is not complete, the calling filter may receive further callbacks and must
   * return an appropriate status code depending on what the filter needs to do.
   */
  virtual void continueDecoding() PURE;

  /**
   * @return RouteConstSharedPtr the route for the current request.
   */
  virtual Router::RouteConstSharedPtr route() PURE;

  /**
   * @return TransportType the originating transport.
   */
  virtual TransportType downstreamTransportType() const PURE;

  /**
   * @return ProtocolType the originating protocol.
   */
  virtual ProtocolType downstreamProtocolType() const PURE;

  /**
   * Create a locally generated response using the provided response object.
   * @param response DirectResponse the response to send to the downstream client
   */
  virtual void sendLocalReply(const ThriftProxy::DirectResponse& response) PURE;

  /**
   * Indicates the start of an upstream response. May only be called once.
   * @param transport_type TransportType the upstream is using
   * @param protocol_type ProtocolType the upstream is using
   */
  virtual void startUpstreamResponse(TransportType transport_type, ProtocolType protocol_type) PURE;

  /**
   * Called with upstream response data.
   * @param data supplies the upstream's data
   * @return true if the upstream response is complete; false if more data is expected
   */
  virtual bool upstreamData(Buffer::Instance& data) PURE;

  /**
   * Reset the downstream connection.
   */
  virtual void resetDownstreamConnection() PURE;
};

enum class FilterStatus {
  // Continue filter chain iteration.
  Continue,

  // Stop iterating over filters in the filter chain. Iteration must be explicitly restarted via
  // continueDecoding().
  StopIteration
};

/**
 * Decoder filter interface.
 */
class DecoderFilter {
public:
  virtual ~DecoderFilter() {}

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

  /**
   * Resets the upstream connection.
   */
  virtual void resetUpstreamConnection() PURE;

  /**
   * Indicates the start of a Thrift transport frame was detected. Unframed transports generate
   * simulated start messages.
   * @param metadata MessageMetadataSharedPtr describing as much as is currently known about the
   *                                          message
   */
  virtual FilterStatus transportBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates the end of a Thrift transport frame was detected. Unframed transport generate
   * simulated complete messages.
   */
  virtual FilterStatus transportEnd() PURE;

  /**
   * Indicates that the start of a Thrift protocol message was detected.
   * @param metadata MessageMetadataSharedPtr describing the message
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageBegin(MessageMetadataSharedPtr metadata) PURE;

  /**
   * Indicates that the end of a Thrift protocol message was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus messageEnd() PURE;

  /**
   * Indicates that the start of a Thrift protocol struct was detected.
   * @param name the name of the struct, if available
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus structBegin(absl::string_view name) PURE;

  /**
   * Indicates that the end of a Thrift protocol struct was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus structEnd() PURE;

  /**
   * Indicates that the start of Thrift protocol struct field was detected.
   * @param name the name of the field, if available
   * @param field_type the type of the field
   * @param field_id the field id
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                                  int16_t field_id) PURE;

  /**
   * Indicates that the end of a Thrift protocol struct field was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus fieldEnd() PURE;

  /**
   * A struct field, map key, map value, list element or set element was detected.
   * @param value type value of the field
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus boolValue(bool value) PURE;
  virtual FilterStatus byteValue(uint8_t value) PURE;
  virtual FilterStatus int16Value(int16_t value) PURE;
  virtual FilterStatus int32Value(int32_t value) PURE;
  virtual FilterStatus int64Value(int64_t value) PURE;
  virtual FilterStatus doubleValue(double value) PURE;
  virtual FilterStatus stringValue(absl::string_view value) PURE;

  /**
   * Indicates the start of a Thrift protocol map was detected.
   * @param key_type the map key type
   * @param value_type the map value type
   * @param size the number of key-value pairs
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus mapBegin(FieldType key_type, FieldType value_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol map was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus mapEnd() PURE;

  /**
   * Indicates the start of a Thrift protocol list was detected.
   * @param elem_type the list value type
   * @param size the number of values in the list
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus listBegin(FieldType elem_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol list was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus listEnd() PURE;

  /**
   * Indicates the start of a Thrift protocol set was detected.
   * @param elem_type the set value type
   * @param size the number of values in the set
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus setBegin(FieldType elem_type, uint32_t size) PURE;

  /**
   * Indicates that the end of a Thrift protocol set was detected.
   * @return FilterStatus to indicate if filter chain iteration should continue
   */
  virtual FilterStatus setEnd() PURE;
};

typedef std::shared_ptr<DecoderFilter> DecoderFilterSharedPtr;

/**
 * These callbacks are provided by the connection manager to the factory so that the factory can
 * build the filter chain in an application specific way.
 */
class FilterChainFactoryCallbacks {
public:
  virtual ~FilterChainFactoryCallbacks() {}

  /**
   * Add a decoder filter that is used when reading connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addDecoderFilter(DecoderFilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a Thrift filter chain for new connections as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
typedef std::function<void(FilterChainFactoryCallbacks& callbacks)> FilterFactoryCb;

/**
 * A FilterChainFactory is used by a connection manager to create a Thrift level filter chain when
 * a new connection is created. Typically it would be implemented by a configuration engine that
 * would install a set of filters that are able to process an application scenario on top of a
 * stream of Thrift requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() {}

  /**
   * Called when a new Thrift stream is created on the connection.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) PURE;
};

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
