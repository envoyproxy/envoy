#pragma once

#include <list>
#include <string>
#include <utility>

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/thrift_proxy/decoder_events.h"
#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"
#include "source/extensions/filters/network/thrift_proxy/transport.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

enum class ResponseStatus {
  MoreData = 0, // The upstream response requires more data.
  Complete = 1, // The upstream response is complete.
  Reset = 2,    // The upstream response is invalid and its connection must be reset.
};

/**
 * Common interface for FilterDecoderCallbacks and FilterEncoderCallbacks.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() const PURE;

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * @return Event::Dispatcher& the thread local dispatcher for allocating timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

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
   * Reset the downstream connection.
   */
  virtual void resetDownstreamConnection() PURE;

  /**
   * @return StreamInfo for logging purposes.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * @return Response decoder metadata created by the connection manager.
   */
  virtual MessageMetadataSharedPtr responseMetadata() PURE;

  /**
   * @return Signal indicating whether or not the response decoder encountered a successful/void
   * reply.
   */
  virtual bool responseSuccess() PURE;

  /**
   * Called when upstream connection gets reset.
   */
  virtual void onReset() PURE;
};

/**
 * Decoder filter callbacks add additional callbacks.
 */
class DecoderFilterCallbacks : public virtual FilterCallbacks {
public:
  ~DecoderFilterCallbacks() override = default;

  /**
   * Indicates the start of an upstream response. May only be called once.
   * @param transport the transport used by the upstream response
   * @param protocol the protocol used by the upstream response
   */
  virtual void startUpstreamResponse(Transport& transport, Protocol& protocol) PURE;

  /**
   * Called with upstream response data.
   * @param data supplies the upstream's data
   * @return ResponseStatus indicating if the upstream response requires more data, is complete,
   *         or if an error occurred requiring the upstream connection to be reset.
   */
  virtual ResponseStatus upstreamData(Buffer::Instance& data) PURE;

  /**
   * Create a locally generated response using the provided response object.
   * @param response DirectResponse the response to send to the downstream client
   * @param end_stream if true, the downstream connection should be closed after this response
   */
  virtual void sendLocalReply(const ThriftProxy::DirectResponse& response, bool end_stream) PURE;

  /**
   * Continue iterating through the filter chain with buffered data. This routine can only be
   * called if the filter has previously returned StopIteration from one of the DecoderFilter
   * methods. The connection manager will callbacks to the next filter in the chain. Further note
   * that if the request is not complete, the calling filter may receive further callbacks and must
   * return an appropriate status code depending on what the filter needs to do.
   */
  virtual void continueDecoding() PURE;
};

/**
 * Encoder filter callbacks add additional callbacks.
 */
class EncoderFilterCallbacks : public virtual FilterCallbacks {
public:
  ~EncoderFilterCallbacks() override = default;

  /**
   * Currently throw given we don't support StopIteration for EncoderFilter yet.
   */
  virtual void continueEncoding() PURE;
};

/**
 * Return codes for onLocalReply filter invocations.
 */
enum class LocalErrorStatus {
  // Continue sending the local reply after onLocalError has been sent to all filters.
  Continue,
};

/**
 * Common interface for Thrift filters.
 */
class FilterBase {
public:
  virtual ~FilterBase() = default;

  /**
   * Called after sendLocalReply is called, and before any local reply is
   * serialized either to filters, or downstream.
   * This will be called on both encoder and decoder filters starting at the
   * first filter and working towards the terminal filter configured (generally the router filter).
   *
   * Filters implementing onLocalReply are responsible for never calling sendLocalReply
   * from onLocalReply, as that has the potential for looping.
   *
   * @param metadata response metadata.
   * @param reset_imminent True if the downstream connection should be closed after this response
   * @param LocalErrorStatus the action to take after onLocalError completes.
   */
  virtual LocalErrorStatus onLocalReply(const MessageMetadata& metadata, bool end_stream) {
    UNREFERENCED_PARAMETER(metadata);
    UNREFERENCED_PARAMETER(end_stream);
    return LocalErrorStatus::Continue;
  }

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
};

using FilterBaseSharedPtr = std::shared_ptr<FilterBase>;

/**
 * Decoder filter interface.
 */
class DecoderFilter : public FilterBase, public virtual DecoderEventHandler {
public:
  ~DecoderFilter() override = default;

  /**
   * Called by the connection manager once to initialize the filter decoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) PURE;

  /**
   * @return True if payload passthrough is supported. Called by the connection manager once after
   * messageBegin.
   */
  virtual bool passthroughSupported() const PURE;
};

using DecoderFilterSharedPtr = std::shared_ptr<DecoderFilter>;

/**
 * Encoder filter interface.
 *
 * Currently the EncoderFilter and BidirectionalFilter support
 * a. peek the metadata and content for encoding,
 * b. modify the metadata and content except string value for encoding, and
 * c. what DecoderFilter supports for BidirectionalFilter.
 *
 * Do not support
 * a. pass through data separately for encode and decode, e.g., pass through data for request but
 * not pass through data for response, and
 * b. return StopIteration for decoder_events in for encoder filter and encode* events for
 * bidirectional filter. The filters trying to return StopIteration will reset the connection.
 */
class EncoderFilter : public FilterBase, public virtual DecoderEventHandler {
public:
  ~EncoderFilter() override = default;

  /**
   * Called by the connection manager once to initialize the filter encoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) PURE;

  /**
   * @return True if payload passthrough is supported. Called by the connection manager once after
   * messageBegin.
   */
  virtual bool passthroughSupported() const PURE;
};

using EncoderFilterSharedPtr = std::shared_ptr<EncoderFilter>;

/**
 * Bidirectional filter interface. @see EncoderFilter for limitation.
 */
class BidirectionalFilter : public FilterBase {
public:
  ~BidirectionalFilter() override = default;
  virtual void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) PURE;
  virtual void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) PURE;
  virtual bool decodePassthroughSupported() const PURE;
  virtual bool encodePassthroughSupported() const PURE;
  virtual FilterStatus decodeTransportBegin(MessageMetadataSharedPtr metadata) PURE;
  virtual FilterStatus encodeTransportBegin(MessageMetadataSharedPtr metadata) PURE;
  virtual FilterStatus decodeTransportEnd() PURE;
  virtual FilterStatus encodeTransportEnd() PURE;
  virtual FilterStatus decodePassthroughData(Buffer::Instance& data) PURE;
  virtual FilterStatus encodePassthroughData(Buffer::Instance& data) PURE;
  virtual FilterStatus decodeMessageBegin(MessageMetadataSharedPtr metadata) PURE;
  virtual FilterStatus encodeMessageBegin(MessageMetadataSharedPtr metadata) PURE;
  virtual FilterStatus decodeMessageEnd() PURE;
  virtual FilterStatus encodeMessageEnd() PURE;
  virtual FilterStatus decodeStructBegin(absl::string_view name) PURE;
  virtual FilterStatus encodeStructBegin(absl::string_view name) PURE;
  virtual FilterStatus decodeStructEnd() PURE;
  virtual FilterStatus encodeStructEnd() PURE;
  virtual FilterStatus decodeFieldBegin(absl::string_view name, FieldType& field_type,
                                        int16_t& field_id) PURE;
  virtual FilterStatus encodeFieldBegin(absl::string_view name, FieldType& field_type,
                                        int16_t& field_id) PURE;
  virtual FilterStatus decodeFieldEnd() PURE;
  virtual FilterStatus encodeFieldEnd() PURE;
  virtual FilterStatus decodeBoolValue(bool& value) PURE;
  virtual FilterStatus encodeBoolValue(bool& value) PURE;
  virtual FilterStatus decodeByteValue(uint8_t& value) PURE;
  virtual FilterStatus encodeByteValue(uint8_t& value) PURE;
  virtual FilterStatus decodeInt16Value(int16_t& value) PURE;
  virtual FilterStatus encodeInt16Value(int16_t& value) PURE;
  virtual FilterStatus decodeInt32Value(int32_t& value) PURE;
  virtual FilterStatus encodeInt32Value(int32_t& value) PURE;
  virtual FilterStatus decodeInt64Value(int64_t& value) PURE;
  virtual FilterStatus encodeInt64Value(int64_t& value) PURE;
  virtual FilterStatus decodeDoubleValue(double& value) PURE;
  virtual FilterStatus encodeDoubleValue(double& value) PURE;
  virtual FilterStatus decodeStringValue(absl::string_view value) PURE;
  virtual FilterStatus encodeStringValue(absl::string_view value) PURE;
  virtual FilterStatus decodeMapBegin(FieldType& key_type, FieldType& value_type,
                                      uint32_t& size) PURE;
  virtual FilterStatus encodeMapBegin(FieldType& key_type, FieldType& value_type,
                                      uint32_t& size) PURE;
  virtual FilterStatus decodeMapEnd() PURE;
  virtual FilterStatus encodeMapEnd() PURE;
  virtual FilterStatus decodeListBegin(FieldType& elem_type, uint32_t& size) PURE;
  virtual FilterStatus encodeListBegin(FieldType& elem_type, uint32_t& size) PURE;
  virtual FilterStatus decodeListEnd() PURE;
  virtual FilterStatus encodeListEnd() PURE;
  virtual FilterStatus decodeSetBegin(FieldType& elem_type, uint32_t& size) PURE;
  virtual FilterStatus encodeSetBegin(FieldType& elem_type, uint32_t& size) PURE;
  virtual FilterStatus decodeSetEnd() PURE;
  virtual FilterStatus encodeSetEnd() PURE;
};

using BidirectionalFilterSharedPtr = std::shared_ptr<BidirectionalFilter>;

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

  /**
   * Add an encoder filter that is used when writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addEncoderFilter(EncoderFilterSharedPtr filter) PURE;

  /**
   * Add a bidirectional filter that is used when reading and writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addBidirectionalFilter(BidirectionalFilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a Thrift filter chain for new connections as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * A FilterChainFactory is used by a connection manager to create a Thrift level filter chain when
 * a new connection is created. Typically it would be implemented by a configuration engine that
 * would install a set of filters that are able to process an application scenario on top of a
 * stream of Thrift requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

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
