#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/stream_info/stream_info.h"

#include "contrib/generic_proxy/filters/network/source/interface/generic_codec.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_route.h"
#include "contrib/generic_proxy/filters/network/source/interface/generic_stream.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

using MetadataUpdateFunction = std::function<void(GenericResponse&)>;

/**
 * The stream filter callbacks are passed to all filters to use for writing response data and
 * interacting with the underlying stream in general.
 */
class StreamFilterCallbacks {
public:
  virtual ~StreamFilterCallbacks() = default;

  /**
   * @return const Network::Connection* the originating connection, or nullptr if there is none.
   */
  virtual const Network::Connection* connection() PURE;

  /**
   * @return Event::Dispatcher& the thread local dispatcher for allocating timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

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
   * Returns the clusterInfo for the cached route.
   */
  virtual Upstream::ClusterInfoConstSharedPtr clusterInfo() PURE;
};

class DecoderFilterCallback : public StreamFilterCallbacks {
public:
  virtual void sendLocalReply(GenericState status, absl::string_view status_detail,
                              MetadataUpdateFunction&& cb = nullptr) PURE;

  virtual void continueDecoding() PURE;

  virtual void upstreamResponse(GenericResponsePtr response) PURE;
};

class EncoderFilterCallback : public StreamFilterCallbacks {
public:
  virtual void continueEncoding() PURE;
};

enum class GenericFilterStatus { Continue, StopIteration };

class DecoderFilter {
public:
  virtual ~DecoderFilter() = default;

  virtual bool isDualFilter() const { return false; }

  virtual void onDestroy() PURE;

  virtual void setDecoderFilterCallbacks(DecoderFilterCallback& callbacks) PURE;
  virtual GenericFilterStatus onStreamDecoded(GenericRequest& request) PURE;
};

class EncoderFilter {
public:
  virtual ~EncoderFilter() = default;

  virtual bool isDualFilter() const { return false; }

  virtual void onDestroy() PURE;

  virtual void setEncoderFilterCallbacks(EncoderFilterCallback& callbacks) PURE;
  virtual GenericFilterStatus onStreamEncoded(GenericResponse& response) PURE;
};

class StreamFilter : public DecoderFilter, public EncoderFilter {
public:
  bool isDualFilter() const override final { return true; }
};

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
 * A FilterChainFactory is used by a connection manager to create a Kafka level filter chain when
 * a new connection is created. Typically it would be implemented by a configuration engine that
 * would install a set of filters that are able to process an application scenario on top of a
 * stream of Dubbo requests.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new Kafka stream is created on the connection.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) PURE;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
