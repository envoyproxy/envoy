#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/transport_socket.h"
#include "envoy/upstream/host_description.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {

namespace Event {
class Dispatcher;
}

namespace Network {

class Connection;
class ConnectionSocket;
class UdpListener;
struct UdpRecvData;

/**
 * Status codes returned by filters that can cause future filters to not get iterated to.
 */
enum class FilterStatus {
  // Continue to further filters.
  Continue,
  // Stop executing further filters.
  StopIteration
};

/**
 * Callbacks used by individual filter instances to communicate with the filter manager.
 */
class NetworkFilterCallbacks {
public:
  virtual ~NetworkFilterCallbacks() = default;

  /**
   * @return the connection that owns this filter.
   */
  virtual Connection& connection() PURE;
};

/**
 * Callbacks used by individual write filter instances to communicate with the filter manager.
 */
class WriteFilterCallbacks : public virtual NetworkFilterCallbacks {
public:
  ~WriteFilterCallbacks() override = default;

  /**
   * Pass data directly to subsequent filters in the filter chain. This method is used in
   * advanced cases in which a filter needs full control over how subsequent filters view data.
   * Using this method allows a filter to buffer data (or not) and then periodically inject data
   * to subsequent filters, indicating end_stream at an appropriate time.
   * This can be used to implement rate limiting, periodic data emission, etc.
   *
   * When using this callback, filters should generally move passed in buffer and return
   * FilterStatus::StopIteration from their onWrite() call, since use of this method
   * indicates that a filter does not wish to participate in a standard write flow
   * and will perform any necessary buffering and continuation on its own.
   *
   * @param data supplies the write data to be propagated directly to further filters in the filter
   *             chain.
   * @param end_stream supplies the end_stream status to be propagated directly to further filters
   *                   in the filter chain.
   */
  virtual void injectWriteDataToFilterChain(Buffer::Instance& data, bool end_stream) PURE;
};

/**
 * A write path binary connection filter.
 */
class WriteFilter {
public:
  virtual ~WriteFilter() = default;

  /**
   * Called when data is to be written on the connection.
   * @param data supplies the buffer to be written which may be modified.
   * @param end_stream supplies whether this is the last byte to write on the connection.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onWrite(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Initializes the write filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered. Thus, any
   * construction that requires the backing connection should take place in the context of this
   * function.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of ReadFilter::onNewConnection() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeWriteFilterCallbacks(WriteFilterCallbacks&) {}
};

using WriteFilterSharedPtr = std::shared_ptr<WriteFilter>;

/**
 * Callbacks used by individual read filter instances to communicate with the filter manager.
 */
class ReadFilterCallbacks : public virtual NetworkFilterCallbacks {
public:
  ~ReadFilterCallbacks() override = default;

  /**
   * If a read filter stopped filter iteration, continueReading() can be called to continue the
   * filter chain. The next filter will be called with all currently available data in the read
   * buffer (it will also have onNewConnection() called on it if it was not previously called).
   */
  virtual void continueReading() PURE;

  /**
   * Pass data directly to subsequent filters in the filter chain. This method is used in
   * advanced cases in which a filter needs full control over how subsequent filters view data,
   * and does not want to make use of connection-level buffering. Using this method allows
   * a filter to buffer data (or not) and then periodically inject data to subsequent filters,
   * indicating end_stream at an appropriate time. This can be used to implement rate limiting,
   * periodic data emission, etc.
   *
   * When using this callback, filters should generally move passed in buffer and return
   * FilterStatus::StopIteration from their onData() call, since use of this method
   * indicates that a filter does not wish to participate in standard connection-level
   * buffering and continuation and will perform any necessary buffering and continuation on its
   * own.
   *
   * This callback is different from continueReading() in that the specified data and end_stream
   * status will be propagated verbatim to further filters in the filter chain
   * (while continueReading() propagates connection-level read buffer and end_stream status).
   *
   * @param data supplies the read data to be propagated directly to further filters in the filter
   *             chain.
   * @param end_stream supplies the end_stream status to be propagated directly to further filters
   *                   in the filter chain.
   */
  virtual void injectReadDataToFilterChain(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Return the currently selected upstream host, if any. This can be used for communication
   * between multiple network level filters, for example the TCP proxy filter communicating its
   * selection to another filter for logging.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() PURE;

  /**
   * Set the currently selected upstream host for the connection.
   */
  virtual void upstreamHost(Upstream::HostDescriptionConstSharedPtr host) PURE;
};

/**
 * A read path binary connection filter.
 */
class ReadFilter {
public:
  virtual ~ReadFilter() = default;

  /**
   * Called when data is read on the connection.
   * @param data supplies the read data which may be modified.
   * @param end_stream supplies whether this is the last byte on the connection. This will only
   *        be set if the connection has half-close semantics enabled.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onData(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Called when a connection is first established. Filters should do one time long term processing
   * that needs to be done when a connection is established. Filter chain iteration can be stopped
   * if needed.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onNewConnection() PURE;

  /**
   * Initializes the read filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered. Thus, any
   * construction that requires the backing connection should take place in the context of this
   * function.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of onNewConnection() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) PURE;
};

using ReadFilterSharedPtr = std::shared_ptr<ReadFilter>;

/**
 * A combination read and write filter. This allows a single filter instance to cover
 * both the read and write paths.
 */
class Filter : public WriteFilter, public ReadFilter {};
using FilterSharedPtr = std::shared_ptr<Filter>;

/**
 * Interface for adding individual network filters to a manager.
 */
class FilterManager {
public:
  virtual ~FilterManager() = default;

  /**
   * Add a write filter to the connection. Filters are invoked in LIFO order (the last added
   * filter is called first).
   */
  virtual void addWriteFilter(WriteFilterSharedPtr filter) PURE;

  /**
   * Add a combination filter to the connection. Equivalent to calling both addWriteFilter()
   * and addReadFilter() with the same filter instance.
   */
  virtual void addFilter(FilterSharedPtr filter) PURE;

  /**
   * Add a read filter to the connection. Filters are invoked in FIFO order (the filter added
   * first is called first).
   */
  virtual void addReadFilter(ReadFilterSharedPtr filter) PURE;

  /**
   * Initialize all of the installed read filters. This effectively calls onNewConnection() on
   * each of them.
   * @return true if read filters were initialized successfully, otherwise false.
   */
  virtual bool initializeReadFilters() PURE;
};

/**
 * This function is used to wrap the creation of a network filter chain for new connections as
 * they come in. Filter factories create the lambda at configuration initialization time, and then
 * they are used at runtime.
 * @param filter_manager supplies the filter manager for the connection to install filters
 * to. Typically the function will install a single filter, but it's technically possibly to
 * install more than one if desired.
 */
using FilterFactoryCb = std::function<void(FilterManager& filter_manager)>;

/**
 * Callbacks used by individual listener filter instances to communicate with the listener filter
 * manager.
 */
class ListenerFilterCallbacks {
public:
  virtual ~ListenerFilterCallbacks() = default;

  /**
   * @return ConnectionSocket the socket the filter is operating on.
   */
  virtual ConnectionSocket& socket() PURE;

  /**
   * @return the Dispatcher for issuing events.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * If a filter stopped filter iteration by returning FilterStatus::StopIteration,
   * the filter should call continueFilterChain(true) when complete to continue the filter chain,
   * or continueFilterChain(false) if the filter execution failed and the connection must be
   * closed.
   * @param success boolean telling whether the filter execution was successful or not.
   */
  virtual void continueFilterChain(bool success) PURE;

  /**
   * @param name the namespace used in the metadata in reverse DNS format, for example:
   * envoy.test.my_filter.
   * @param value the struct to set on the namespace. A merge will be performed with new values for
   * the same key overriding existing.
   */
  virtual void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) PURE;

  /**
   * @return const envoy::api::v2::core::Metadata& the dynamic metadata associated with this
   * connection.
   */
  virtual envoy::config::core::v3::Metadata& dynamicMetadata() PURE;
  virtual const envoy::config::core::v3::Metadata& dynamicMetadata() const PURE;
};

/**
 *  Interface for a listener filter matching with incoming traffic.
 */
class ListenerFilterMatcher {
public:
  virtual ~ListenerFilterMatcher() = default;
  virtual bool matches(Network::ListenerFilterCallbacks& cb) const PURE;
};
using ListenerFilterMatcherPtr = std::unique_ptr<ListenerFilterMatcher>;
using ListenerFilterMatcherSharedPtr = std::shared_ptr<ListenerFilterMatcher>;

/**
 * Listener Filter
 */
class ListenerFilter {
public:
  virtual ~ListenerFilter() = default;

  /**
   * Called when a new connection is accepted, but before a Connection is created.
   * Filter chain iteration can be stopped if needed.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) PURE;
};

using ListenerFilterPtr = std::unique_ptr<ListenerFilter>;

/**
 * Interface for filter callbacks and adding listener filters to a manager.
 */
class ListenerFilterManager {
public:
  virtual ~ListenerFilterManager() = default;

  /**
   * Add a filter to the listener. Filters are invoked in FIFO order (the filter added
   * first is called first).
   * @param listener_filter_matcher supplies the matcher to decide when filter is enabled.
   * @param filter supplies the filter being added.
   */
  virtual void addAcceptFilter(const ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                               ListenerFilterPtr&& filter) PURE;
};

/**
 * This function is used to wrap the creation of a listener filter chain for new sockets as they are
 * created. Filter factories create the lambda at configuration initialization time, and then they
 * are used at runtime.
 * @param filter_manager supplies the filter manager for the listener to install filters to.
 * Typically the function will install a single filter, but it's technically possibly to install
 * more than one if desired.
 */
using ListenerFilterFactoryCb = std::function<void(ListenerFilterManager& filter_manager)>;

/**
 * Interface representing a single filter chain.
 */
class FilterChain {
public:
  virtual ~FilterChain() = default;

  /**
   * @return const TransportSocketFactory& a transport socket factory to be used by the new
   * connection.
   */
  virtual const TransportSocketFactory& transportSocketFactory() const PURE;

  /**
   * const std::vector<FilterFactoryCb>& a list of filters to be used by the new connection.
   */
  virtual const std::vector<FilterFactoryCb>& networkFilterFactories() const PURE;
};

using FilterChainSharedPtr = std::shared_ptr<FilterChain>;

/**
 * A filter chain that can be drained.
 */
class DrainableFilterChain : public FilterChain {
public:
  virtual void startDraining() PURE;
};

using DrainableFilterChainSharedPtr = std::shared_ptr<DrainableFilterChain>;

/**
 * Interface for searching through configured filter chains.
 */
class FilterChainManager {
public:
  virtual ~FilterChainManager() = default;

  /**
   * Find filter chain that's matching metadata from the new connection.
   * @param socket supplies connection metadata that's going to be used for the filter chain lookup.
   * @return const FilterChain* filter chain to be used by the new connection,
   *         nullptr if no matching filter chain was found.
   */
  virtual const FilterChain* findFilterChain(const ConnectionSocket& socket) const PURE;
};

/**
 * Callbacks used by individual UDP listener read filter instances to communicate with the filter
 * manager.
 */
class UdpReadFilterCallbacks {
public:
  virtual ~UdpReadFilterCallbacks() = default;

  /**
   * @return the udp listener that owns this read filter.
   */
  virtual UdpListener& udpListener() PURE;
};

/**
 * UDP Listener Read Filter
 */
class UdpListenerReadFilter {
public:
  virtual ~UdpListenerReadFilter() = default;

  /**
   * Called when a new data packet is received on a UDP listener.
   * @param data supplies the read data which may be modified.
   */
  virtual void onData(UdpRecvData& data) PURE;

  /**
   * Called when there is an error event in the receive data path.
   *
   * @param error_code supplies the received error on the listener.
   */
  virtual void onReceiveError(Api::IoError::IoErrorCode error_code) PURE;

protected:
  /**
   * @param callbacks supplies the read filter callbacks used to interact with the filter manager.
   */
  UdpListenerReadFilter(UdpReadFilterCallbacks& callbacks) : read_callbacks_(&callbacks) {}

  UdpReadFilterCallbacks* read_callbacks_{};
};

using UdpListenerReadFilterPtr = std::unique_ptr<UdpListenerReadFilter>;

/**
 * Interface for adding UDP listener filters to a manager.
 */
class UdpListenerFilterManager {
public:
  virtual ~UdpListenerFilterManager() = default;

  /**
   * Add a read filter to the udp listener. Filters are invoked in FIFO order (the
   * filter added first is called first).
   * @param filter supplies the filter being added.
   */
  virtual void addReadFilter(UdpListenerReadFilterPtr&& filter) PURE;
};

using UdpListenerFilterFactoryCb = std::function<void(
    UdpListenerFilterManager& udp_listener_filter_manager, UdpReadFilterCallbacks& callbacks)>;

/**
 * Creates a chain of network filters for a new connection.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called to create the network filter chain.
   * @param connection supplies the connection to create the chain on.
   * @param filter_factories supplies a list of filter factories to create the chain from.
   * @return true if filter chain was created successfully. Otherwise
   *   false, e.g. filter chain is empty.
   */
  virtual bool createNetworkFilterChain(Connection& connection,
                                        const std::vector<FilterFactoryCb>& filter_factories) PURE;

  /**
   * Called to create the listener filter chain.
   * @param listener supplies the listener to create the chain on.
   * @return true if filter chain was created successfully. Otherwise false.
   */
  virtual bool createListenerFilterChain(ListenerFilterManager& listener) PURE;

  /**
   * Called to create a Udp Listener Filter Chain object
   *
   * @param udp_listener supplies the listener to create the chain on.
   * @param callbacks supplies the callbacks needed to create a filter.
   */
  virtual void createUdpListenerFilterChain(UdpListenerFilterManager& udp_listener,
                                            UdpReadFilterCallbacks& callbacks) PURE;
};

} // namespace Network
} // namespace Envoy
