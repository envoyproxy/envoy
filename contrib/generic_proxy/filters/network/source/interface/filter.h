#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "contrib/generic_proxy/filters/network/source/interface/codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/route.h"
#include "contrib/generic_proxy/filters/network/source/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

using ResponseUpdateFunction = std::function<void(Response&)>;

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
  virtual const CodecFactory& downstreamCodec() PURE;

  /**
   * Reset the underlying stream.
   */
  virtual void resetStream() PURE;

  /**
   * @return const RouteEntry* cached route entry for current request.
   */
  virtual const RouteEntry* routeEntry() const PURE;

  /**
   * @return const RouteSpecificFilterConfig* route level per filter config. The filter config
   * name will be used to get the config.
   */
  virtual const RouteSpecificFilterConfig* perFilterConfig() const PURE;
};

class DecoderFilterCallback : public virtual StreamFilterCallbacks {
public:
  virtual void sendLocalReply(Status status, ResponseUpdateFunction&& cb = nullptr) PURE;

  virtual void continueDecoding() PURE;

  virtual void upstreamResponse(ResponsePtr response) PURE;

  virtual void completeDirectly() PURE;
};

class EncoderFilterCallback : public virtual StreamFilterCallbacks {
public:
  virtual void continueEncoding() PURE;
};

enum class FilterStatus { Continue, StopIteration };

class DecoderFilter {
public:
  virtual ~DecoderFilter() = default;

  virtual void onDestroy() PURE;

  virtual void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) PURE;
  virtual FilterStatus onStreamDecoded(Request& request) PURE;
};

class EncoderFilter {
public:
  virtual ~EncoderFilter() = default;

  virtual void onDestroy() PURE;

  virtual void setEncoderFilterCallbacks(EncoderFilterCallback& callbacks) PURE;
  virtual FilterStatus onStreamEncoded(Response& response) PURE;
};

class StreamFilter : public DecoderFilter, public EncoderFilter {};

using DecoderFilterSharedPtr = std::shared_ptr<DecoderFilter>;
using EncoderFilterSharedPtr = std::shared_ptr<EncoderFilter>;
using StreamFilterSharedPtr = std::shared_ptr<StreamFilter>;

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
  virtual void addFilter(StreamFilterSharedPtr filter) PURE;
};

using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * Simple struct of additional contextual information of filter, e.g. filter config name
 * from configuration.
 */
struct FilterContext {
  // The name of the filter configuration that used to create related filter factory function.
  // This could be any legitimate non-empty string.
  // This config name will have longger lifetime than any related filter instance. So string
  // view could be used here safely.
  absl::string_view config_name;
};

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
