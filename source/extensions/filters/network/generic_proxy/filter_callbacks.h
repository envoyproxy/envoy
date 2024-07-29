#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/generic_proxy/interface/codec.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"
#include "source/extensions/filters/network/generic_proxy/route.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ResponseUpdateFunction = std::function<void(ResponseHeaderFrame&)>;

/**
 * RequestFramesHandler to handle the frames from the stream (if exists).
 */
class RequestFramesHandler {
public:
  virtual ~RequestFramesHandler() = default;

  /**
   * Handle the frame from the stream.
   * @param frame frame from the stream.
   */
  virtual void onRequestCommonFrame(RequestCommonFramePtr frame) PURE;
};

/**
 * The stream filter callbacks are passed to all filters to use for writing response data and
 * interacting with the underlying stream in general.
 */
class StreamFilterCallbacks {
public:
  virtual ~StreamFilterCallbacks() = default;

  /**
   * @return Event::Dispatcher& the thread local dispatcher for allocating timers, etc.
   */
  virtual Envoy::Event::Dispatcher& dispatcher() PURE;

  /**
   * @return const CodecFactory& the downstream codec factory used to create request/response
   * decoder/encoder.
   */
  virtual const CodecFactory& codecFactory() PURE;

  /**
   * @return const RouteEntry* cached route entry for current request.
   */
  virtual const RouteEntry* routeEntry() const PURE;

  /**
   * @return const RouteSpecificFilterConfig* route level per filter config. The filter config
   * name will be used to get the config.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig() const PURE;

  /**
   * @return StreamInfo::StreamInfo& the stream info object associated with the stream.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * @return Tracing::Span& the active span associated with the stream.
   */
  virtual Tracing::Span& activeSpan() PURE;

  /**
   * @return const Tracing::Config& the tracing configuration.
   */
  virtual OptRef<const Tracing::Config> tracingConfig() const PURE;

  /**
   * @return const Network::Connection* downstream connection.
   */
  virtual const Network::Connection* connection() const PURE;

  /**
   * @return absl::string_view the filter config name that used to create the filter.
   */
  virtual absl::string_view filterConfigName() const PURE;
};

class DecoderFilterCallback : public virtual StreamFilterCallbacks {
public:
  /**
   * Send local reply directly to the downstream for the current request. Note encoder filters
   * will be skipped for the local reply for now.
   * @param status supplies the protocol independent response status to the codec to create
   * actual response frame or message. Note the actual response code may be different with code
   * in the status. For example, if the status is Protocol::Status::Ok, the actual response code
   * may be 200 for HTTP/1.1 or 20 for Dubbo.
   * The status message will be used as response code details and could be logged.
   * @param data supplies the additional data to the codec to create actual response frame or
   * message. This could be anything and is optional.
   * @param cb supplies the callback to update the response. This is optional and could be nullptr.
   */
  virtual void sendLocalReply(Status status, absl::string_view data = {},
                              ResponseUpdateFunction cb = {}) PURE;

  virtual void continueDecoding() PURE;

  /**
   * Called when the upstream response frame is received. This should only be called once.
   * @param frame supplies the upstream response frame.
   */
  virtual void onResponseHeaderFrame(ResponseHeaderFramePtr frame) PURE;

  /**
   * Called when the upstream response frame is received.
   * @param frame supplies the upstream frame.
   */
  virtual void onResponseCommonFrame(ResponseCommonFramePtr frame) PURE;

  using RequestCommonFrameHandler = std::function<void(RequestCommonFramePtr)>;

  /**
   * Register a request frames handler to take the ownership of the common frames from the stream
   * (if exists). This handler will be called after a common frame is processed by the L7 filter
   * chain.
   *
   * @param handler supplies the request frames handler.
   * NOTE: This handler should be set by the terminal filter only.
   */
  virtual void setRequestFramesHandler(RequestFramesHandler* handler) PURE;

  virtual void completeDirectly() PURE;
};

class EncoderFilterCallback : public virtual StreamFilterCallbacks {
public:
  virtual void continueEncoding() PURE;
};

class DecoderFilter;
class EncoderFilter;
class StreamFilter;

class FilterChainFactoryCallbacks {
public:
  virtual ~FilterChainFactoryCallbacks() = default;

  /**
   * Add a decoder filter that is used when reading connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addDecoderFilter(std::shared_ptr<DecoderFilter> filter) PURE;

  /**
   * Add a encoder filter that is used when writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addEncoderFilter(std::shared_ptr<EncoderFilter> filter) PURE;

  /**
   * Add a decoder/encoder filter that is used both when reading and writing connection data.
   * @param filter supplies the filter to add.
   */
  virtual void addFilter(std::shared_ptr<StreamFilter> filter) PURE;
};

/**
 * Simple struct of additional contextual information of filter, e.g. filter config name
 * from configuration.
 */
struct FilterContext {
  // The name of the filter configuration that used to create related filter factory function.
  // This could be any legitimate non-empty string.
  // This config name will have longer lifetime than any related filter instance. So string
  // view could be used here safely.
  absl::string_view config_name;
};

using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * The filter chain manager is provided by the connection manager to the filter chain factory.
 * The filter chain factory will post the filter factory context and filter factory to the
 * filter chain manager to create filter and construct HTTP stream filter chain.
 */
class FilterChainManager {
public:
  virtual ~FilterChainManager() = default;

  /**
   * Post filter factory context and filter factory to the filter chain manager. The filter
   * chain manager will create filter instance based on the context and factory internally.
   * @param context supplies additional contextual information of filter factory.
   * @param factory factory function used to create filter instances.
   */
  virtual void applyFilterFactoryCb(FilterContext context, FilterFactoryCb& factory) PURE;
};

/**
 * A FilterChainFactory is used by a connection manager to create a stream level filter chain
 * when a new stream is created. Typically it would be implemented by a configuration engine
 * that would install a set of filters that are able to process an application scenario on top of a
 * stream of generic requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new HTTP stream is created on the connection.
   * @param manager supplies the "sink" that is used for actually creating the filter chain. @see
   *                FilterChainManager.
   */
  virtual void createFilterChain(FilterChainManager& manager) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
