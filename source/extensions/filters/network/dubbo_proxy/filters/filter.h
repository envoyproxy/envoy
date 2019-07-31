#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "extensions/filters/network/dubbo_proxy/decoder_event_handler.h"
#include "extensions/filters/network/dubbo_proxy/message.h"
#include "extensions/filters/network/dubbo_proxy/metadata.h"
#include "extensions/filters/network/dubbo_proxy/protocol.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"
#include "extensions/filters/network/dubbo_proxy/serializer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace DubboFilters {

enum class UpstreamResponseStatus : uint8_t {
  MoreData = 0, // The upstream response requires more data.
  Complete = 1, // The upstream response is complete.
  Reset = 2,    // The upstream response is invalid and its connection must be reset.
  Retry = 3,    // The upstream response is failure need to retry.
};

class DirectResponse {
public:
  virtual ~DirectResponse() = default;

  enum class ResponseType : uint8_t {
    // DirectResponse encodes MessageType::Reply with success payload
    SuccessReply,

    // DirectResponse encodes MessageType::Reply with an exception payload
    ErrorReply,

    // DirectResponse encodes MessageType::Exception
    Exception,
  };

  /**
   * Encodes the response via the given Protocol.
   * @param metadata the MessageMetadata for the request that generated this response
   * @param proto the Protocol to be used for message encoding
   * @param buffer the Buffer into which the message should be encoded
   * @return ResponseType indicating whether the message is a successful or error reply or an
   *         exception
   */
  virtual ResponseType encode(MessageMetadata& metadata, Protocol& protocol,
                              Buffer::Instance& buffer) const PURE;
};

using DirectResponsePtr = std::unique_ptr<DirectResponse>;

/**
 * Decoder filter callbacks add additional callbacks.
 */
class FilterCallbacksBase {
public:
  virtual ~FilterCallbacksBase() = default;

  /**
   * @return uint64_t the ID of the originating request for logging purposes.
   */
  virtual uint64_t requestId() const PURE;

  /**
   * @return uint64_t the ID of the originating stream for logging purposes.
   */
  virtual uint64_t streamId() const PURE;

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * @return RouteConstSharedPtr the route for the current request.
   */
  virtual DubboProxy::Router::RouteConstSharedPtr route() PURE;

  /**
   * @return SerializationType the originating protocol.
   */
  virtual SerializationType serializationType() const PURE;

  /**
   * @return ProtocolType the originating protocol.
   */
  virtual ProtocolType protocolType() const PURE;

  /**
   * @return StreamInfo for logging purposes.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * @return Event::Dispatcher& the thread local dispatcher for allocating timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Reset the underlying stream.
   */
  virtual void resetStream() PURE;
};

/**
 * Decoder filter callbacks add additional callbacks.
 */
class DecoderFilterCallbacks : public virtual FilterCallbacksBase {
public:
  ~DecoderFilterCallbacks() override = default;

  /**
   * Continue iterating through the filter chain with buffered data. This routine can only be
   * called if the filter has previously returned StopIteration from one of the DecoderFilter
   * methods. The connection manager will callbacks to the next filter in the chain. Further note
   * that if the request is not complete, the calling filter may receive further callbacks and must
   * return an appropriate status code depending on what the filter needs to do.
   */
  virtual void continueDecoding() PURE;

  /**
   * Create a locally generated response using the provided response object.
   * @param response DirectResponsePtr the response to send to the downstream client
   */
  virtual void sendLocalReply(const DirectResponse& response, bool end_stream) PURE;

  /**
   * Indicates the start of an upstream response. May only be called once.
   * @param transport_type TransportType the upstream is using
   * @param protocol_type ProtocolType the upstream is using
   */
  virtual void startUpstreamResponse() PURE;

  /**
   * Called with upstream response data.
   * @param data supplies the upstream's data
   * @return UpstreamResponseStatus indicating if the upstream response requires more data, is
   * complete, or if an error occurred requiring the upstream connection to be reset.
   */
  virtual UpstreamResponseStatus upstreamData(Buffer::Instance& data) PURE;

  /**
   * Reset the downstream connection.
   */
  virtual void resetDownstreamConnection() PURE;
};

/**
 * Encoder filter callbacks add additional callbacks.
 */
class EncoderFilterCallbacks : public virtual FilterCallbacksBase {
public:
  ~EncoderFilterCallbacks() override = default;

  /**
   * Continue iterating through the filter chain with buffered data. This routine can only be
   * called if the filter has previously returned StopIteration from one of the DecoderFilter
   * methods. The connection manager will callbacks to the next filter in the chain. Further note
   * that if the request is not complete, the calling filter may receive further callbacks and must
   * return an appropriate status code depending on what the filter needs to do.
   */
  virtual void continueEncoding() PURE;
};

/**
 * Common base class for both decoder and encoder filters.
 */
class FilterBase {
public:
  virtual ~FilterBase() = default;

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

/**
 * Decoder filter interface.
 */
class DecoderFilter : public StreamDecoder, public FilterBase {
public:
  ~DecoderFilter() override = default;

  /**
   * Called by the connection manager once to initialize the filter decoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) PURE;
};

using DecoderFilterSharedPtr = std::shared_ptr<DecoderFilter>;

/**
 * Encoder filter interface.
 */
class EncoderFilter : public StreamEncoder, public FilterBase {
public:
  ~EncoderFilter() override = default;

  /**
   * Called by the connection manager once to initialize the filter encoder callbacks that the
   * filter should use. Callbacks will not be invoked by the filter after onDestroy() is called.
   */
  virtual void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) PURE;
};

using EncoderFilterSharedPtr = std::shared_ptr<EncoderFilter>;

/**
 * A filter that handles both encoding and decoding.
 */
class CodecFilter : public virtual DecoderFilter, public virtual EncoderFilter {};

using CodecFilterSharedPtr = std::shared_ptr<CodecFilter>;

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
   * Add a encoder filter that is used when writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addEncoderFilter(EncoderFilterSharedPtr filter) PURE;

  /**
   * Add a decoder/encoder filter that is used both when reading and writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addFilter(CodecFilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a Dubbo filter chain for new connections as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * A FilterChainFactory is used by a connection manager to create a Dubbo level filter chain when
 * a new connection is created. Typically it would be implemented by a configuration engine that
 * would install a set of filters that are able to process an application scenario on top of a
 * stream of Dubbo requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new Dubbo stream is created on the connection.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) PURE;
};

} // namespace DubboFilters
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
