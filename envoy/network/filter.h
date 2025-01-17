#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/config/extension_config_provider.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener_filter_buffer.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/host_description.h"

#include "source/common/protobuf/protobuf.h"

namespace quic {
class QuicSocketAddress;
class QuicReceivedPacket;
} // namespace quic

namespace Envoy {

namespace Event {
class Dispatcher;
}

namespace Network {

class Connection;
class ConnectionSocket;
class Socket;
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

  /**
   * @return Socket the socket the filter is operating on.
   */
  virtual const Socket& socket() PURE;
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

  /**
   * Signal to the filter manager to enable secure transport mode in upstream connection.
   * This is done when upstream connection's transport socket is of startTLS type. At the moment
   * it is the only transport socket type which can be programmatically converted from non-secure
   * mode to secure mode.
   */
  virtual bool startUpstreamSecureTransport() PURE;
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

  /**
   * Method is called by the filter manager to convert upstream's connection transport socket
   * from non-secure mode to secure mode. Only terminal filters are aware of upstream connection and
   * non-terminal filters should not implement startUpstreamSecureTransport.
   */
  virtual bool startUpstreamSecureTransport() { return false; }
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
   * Remove a read filter from the connection.
   */
  virtual void removeReadFilter(ReadFilterSharedPtr filter) PURE;

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
   * @param name the namespace used in the metadata in reverse DNS format, for example:
   * envoy.test.my_filter.
   * @param value the struct to set on the namespace. A merge will be performed with new values for
   * the same key overriding existing.
   */
  virtual void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) PURE;

  /**
   * @param name the namespace used in the metadata in reverse DNS format, for example:
   * envoy.test.my_filter.
   * @param value of type protobuf any to set on the namespace. A merge will be performed with new
   * values for the same key overriding existing.
   */
  virtual void setDynamicTypedMetadata(const std::string& name, const ProtobufWkt::Any& value) PURE;

  /**
   * @return const envoy::config::core::v3::Metadata& the dynamic metadata associated with this
   * connection.
   */
  virtual envoy::config::core::v3::Metadata& dynamicMetadata() PURE;
  virtual const envoy::config::core::v3::Metadata& dynamicMetadata() const PURE;

  /**
   * @return Object on which filters can share data on a per-request basis.
   */
  virtual StreamInfo::FilterState& filterState() PURE;

  /**
   * @return the Dispatcher for issuing events.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * If a filter returned `FilterStatus::ContinueIteration`, `continueFilterChain(true)`
   * should be called to continue the filter chain iteration. Or `continueFilterChain(false)`
   * should be called if the filter returned `FilterStatus::StopIteration` and closed
   * the socket.
   * @param success boolean telling whether the filter execution was successful or not.
   */
  virtual void continueFilterChain(bool success) PURE;

  /**
   * Overwrite the default use original dst setting for current connection. This allows the
   * listener filter to control whether the connection should be forwarded to other listeners
   * based on the original destination address or not.
   * @param use_original_dst whether to use original destination address or not.
   */
  virtual void useOriginalDst(bool use_original_dst) PURE;
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
 * TCP Listener Filter
 */
class ListenerFilter {
public:
  virtual ~ListenerFilter() = default;

  /**
   * Called when a new connection is accepted, but before a Connection is created.
   * Filter chain iteration can be stopped if need more data from the connection
   * by returning `FilterStatus::StopIteration`, or continue the filter chain iteration
   * by returning `FilterStatus::ContinueIteration`. Reject the connection by closing
   * the socket and returning `FilterStatus::StopIteration`.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) PURE;

  /**
   * Called when data is read from the connection. If the filter doesn't get
   * enough data, filter chain iteration can be stopped if needed by returning
   * `FilterStatus::StopIteration`. Or continue the filter chain iteration by returning
   * `FilterStatus::ContinueIteration` if the filter get enough data. Reject the connection
   * by closing the socket and returning `FilterStatus::StopIteration`.
   * @param buffer the buffer of data.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onData(Network::ListenerFilterBuffer& buffer) PURE;

  /**
   * Return the size of data the filter want to inspect from the connection.
   * The size can be increased after filter need to inspect more data.
   * @return maximum number of bytes of the data consumed by the filter. 0 means filter does not
   * need any data.
   */
  virtual size_t maxReadBytes() const PURE;
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
 * QUIC Listener Filter
 */
class QuicListenerFilter {
public:
  virtual ~QuicListenerFilter() = default;

  /**
   * Called when a new connection is accepted, but before a Connection is created.
   * Filter chain iteration can be terminated if the cb shouldn't be accessed any more
   * by returning `FilterStatus::StopIteration`, or continue the filter chain iteration
   * by returning `FilterStatus::ContinueIteration`. Reject the connection by closing
   * the socket and returning `FilterStatus::StopIteration`. If `FilterStatus::StopIteration` is
   * returned, following filters' onAccept() will be skipped, but the connection creation is not
   * going to be paused. If the connection socket is closed upon connection creation, the connection
   * will be closed immediately.
   * @param cb the callbacks the filter instance can use to communicate with the filter chain.
   * @param server_preferred_addresses the server's preferred addresses to be advertised.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) PURE;

  /**
   * Called before connection creation.
   * @return false if the given preferred address is incomplatible with this filter and the listener
   * shouldn't advertise the given preferred address. I.e. onAccept() would have behaved differently
   * if the connection socket's destination address were the preferred address.
   */
  virtual bool isCompatibleWithServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const PURE;

  /**
   * Called after the peer has migrated to a different address. Check if the connection
   * migration is compatible with this listener filter. If not, close the connection and return
   * `FilterStatus::StopIteration`. An alternative approach is to disable active migration on the
   * given connection during connection creation. But peer address change is inevitable given NAT
   * rebinding is more frequent than TCP, and such passive address change is indistinguishable from
   * active migration. So there is no way to completely disable connection migration. disable client
   * connection migration.
   * @param new_address the address the peer has migrated to.
   * @param connection the connection just migrated.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                                            Connection& connection) PURE;

  /**
   * Called when the QUIC server session processes its first packet.
   * @param packet the received packet.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onFirstPacketReceived(const quic::QuicReceivedPacket&) PURE;
};

using QuicListenerFilterPtr = std::unique_ptr<QuicListenerFilter>;

/**
 * Interface for filter callbacks and adding listener filters to a manager.
 */
class QuicListenerFilterManager {
public:
  virtual ~QuicListenerFilterManager() = default;

  /**
   * Add a filter to the listener. Filters are invoked in FIFO order (the filter added
   * first is called first).
   * @param listener_filter_matcher supplies the matcher to decide when filter is enabled.
   * @param filter supplies the filter being added.
   */
  virtual void addFilter(const ListenerFilterMatcherSharedPtr& listener_filter_matcher,
                         QuicListenerFilterPtr&& filter) PURE;

  virtual bool shouldAdvertiseServerPreferredAddress(
      const quic::QuicSocketAddress& server_preferred_address) const PURE;

  virtual void onPeerAddressChanged(const quic::QuicSocketAddress& new_address,
                                    Connection& connection) PURE;
  virtual void onFirstPacketReceived(const quic::QuicReceivedPacket&) PURE;
};

/**
 * This function is used to wrap the creation of a QUIC listener filter chain for new connections.
 * Filter factories create the lambda at configuration initialization time, and then they are used
 * at runtime.
 * @param filter_manager supplies the filter manager for the listener to install filters to.
 * Typically the function will install a single filter, but it's technically possibly to install
 * more than one if desired.
 */
using QuicListenerFilterFactoryCb = std::function<void(QuicListenerFilterManager& filter_manager)>;

template <class FactoryCb>
using FilterConfigProvider = Envoy::Config::ExtensionConfigProvider<FactoryCb>;

template <class FactoryCb>
using FilterConfigProviderPtr = std::unique_ptr<FilterConfigProvider<FactoryCb>>;

using NetworkFilterFactoriesList = std::vector<FilterConfigProviderPtr<FilterFactoryCb>>;

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
  virtual const DownstreamTransportSocketFactory& transportSocketFactory() const PURE;

  /**
   * @return std::chrono::milliseconds the amount of time to wait for the transport socket to report
   * that a connection has been established. If the timeout is reached, the connection is closed. 0
   * specifies a disabled timeout.
   */
  virtual std::chrono::milliseconds transportSocketConnectTimeout() const PURE;

  /**
   * @return const Filter::NetworkFilterFactoriesList& a list of filter factory providers to be
   * used by the new connection.
   */
  virtual const NetworkFilterFactoriesList& networkFilterFactories() const PURE;

  /**
   * @return the name of this filter chain.
   */
  virtual absl::string_view name() const PURE;
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
   * @param info supplies the dynamic metadata and the filter state populated by the listener
   * filters.
   * @return const FilterChain* filter chain to be used by the new connection,
   *         nullptr if no matching filter chain was found.
   */
  virtual const FilterChain* findFilterChain(const ConnectionSocket& socket,
                                             const StreamInfo::StreamInfo& info) const PURE;
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
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onData(UdpRecvData& data) PURE;

  /**
   * Called when there is an error event in the receive data path.
   *
   * @param error_code supplies the received error on the listener.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual FilterStatus onReceiveError(Api::IoError::IoErrorCode error_code) PURE;

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
 * Common interface for UdpSessionReadFilterCallbacks and UdpSessionWriteFilterCallbacks.
 */
class UdpSessionFilterCallbacks {
public:
  virtual ~UdpSessionFilterCallbacks() = default;

  /**
   * @return uint64_t the ID of the originating UDP session.
   */
  virtual uint64_t sessionId() const PURE;

  /**
   * @return StreamInfo for logging purposes.
   */
  virtual StreamInfo::StreamInfo& streamInfo() PURE;

  /**
   * Allows a filter to inject a datagram to successive filters in the session filter chain.
   * The injected datagram will be iterated as a regular received datagram, and may also be
   * stopped by further filters. This can be used, for example, to continue processing previously
   * buffered datagrams by a filter after an asynchronous operation ended.
   */
  virtual void injectDatagramToFilterChain(Network::UdpRecvData& data) PURE;
};

class UdpSessionReadFilterCallbacks : public UdpSessionFilterCallbacks {
public:
  ~UdpSessionReadFilterCallbacks() override = default;

  /**
   * If a read filter stopped filter iteration, continueFilterChain() can be called to continue the
   * filter chain. It will have onNewSession() called if it was not previously called.
   * @return false if the session is removed and no longer valid, otherwise returns true.
   */
  virtual bool continueFilterChain() PURE;
};

class UdpSessionWriteFilterCallbacks : public UdpSessionFilterCallbacks {};

class UdpSessionFilterBase {
public:
  virtual ~UdpSessionFilterBase() = default;

  /**
   * This routine is called before the access log handlers' final log() is called. Filters can use
   * this callback to enrich the data passed in to the log handlers.
   */
  void onSessionComplete() {
    if (!on_session_complete_already_called_) {
      onSessionCompleteInternal();
      on_session_complete_already_called_ = true;
    }
  }

protected:
  /**
   * This routine is called by onSessionComplete to enrich the data passed in to the log handlers.
   */
  virtual void onSessionCompleteInternal() { ASSERT(!on_session_complete_already_called_); }

private:
  bool on_session_complete_already_called_{false};
};

/**
 * Return codes for read filter invocations.
 */
enum class UdpSessionReadFilterStatus {
  // Continue to further session filters.
  Continue,
  // Stop executing further session filters.
  StopIteration,
};

/**
 * Session read filter interface.
 */
class UdpSessionReadFilter : public virtual UdpSessionFilterBase {
public:
  ~UdpSessionReadFilter() override = default;

  /**
   * Called when a new UDP session is first established. Filters should do one time long term
   * processing that needs to be done when a session is established. Filter chain iteration
   * can be stopped if needed.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual UdpSessionReadFilterStatus onNewSession() PURE;

  /**
   * Called when UDP datagram is read and matches the session that manages the filter.
   * @param data supplies the read data which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual UdpSessionReadFilterStatus onData(Network::UdpRecvData& data) PURE;

  /**
   * Initializes the read filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of onNewSession() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeReadFilterCallbacks(UdpSessionReadFilterCallbacks& callbacks) PURE;
};

using UdpSessionReadFilterSharedPtr = std::shared_ptr<UdpSessionReadFilter>;

/**
 * Return codes for write filter invocations.
 */
enum class UdpSessionWriteFilterStatus {
  // Continue to further session filters.
  Continue,
  // Stop executing further session filters.
  StopIteration,
};

/**
 * Session write filter interface.
 */
class UdpSessionWriteFilter : public virtual UdpSessionFilterBase {
public:
  ~UdpSessionWriteFilter() override = default;

  /**
   * Called when data is to be written on the UDP session.
   * @param data supplies the buffer to be written which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual UdpSessionWriteFilterStatus onWrite(Network::UdpRecvData& data) PURE;

  /**
   * Initializes the write filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of ReadFilter::onNewSession() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeWriteFilterCallbacks(UdpSessionWriteFilterCallbacks& callbacks) PURE;
};

using UdpSessionWriteFilterSharedPtr = std::shared_ptr<UdpSessionWriteFilter>;

/**
 * A combination read and write filter. This allows a single filter instance to cover
 * both the read and write paths.
 */
class UdpSessionFilter : public virtual UdpSessionReadFilter,
                         public virtual UdpSessionWriteFilter {};
using UdpSessionFilterSharedPtr = std::shared_ptr<UdpSessionFilter>;

/**
 * These callbacks are provided by the UDP session manager to the factory so that the factory
 * can * build the filter chain in an application specific way.
 */
class UdpSessionFilterChainFactoryCallbacks {
public:
  virtual ~UdpSessionFilterChainFactoryCallbacks() = default;

  /**
   * Add a read filter that is used when reading UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addReadFilter(UdpSessionReadFilterSharedPtr filter) PURE;

  /**
   * Add a write filter that is used when writing UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addWriteFilter(UdpSessionWriteFilterSharedPtr filter) PURE;

  /**
   * Add a bidirectional filter that is used when reading and writing UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addFilter(UdpSessionFilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a UDP session filter chain for new sessions as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using UdpSessionFilterFactoryCb =
    std::function<void(UdpSessionFilterChainFactoryCallbacks& callbacks)>;

/**
 * A UdpSessionFilterChainFactory is used by a UDP session manager to create a UDP session filter
 * chain when a new session is created.
 */
class UdpSessionFilterChainFactory {
public:
  virtual ~UdpSessionFilterChainFactory() = default;

  /**
   * Called when a new UDP session is created.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  UdpSessionFilterChainFactoryCallbacks.
   * @return true if filter chain was created successfully. Otherwise false.
   */
  virtual bool createFilterChain(UdpSessionFilterChainFactoryCallbacks& callbacks) const PURE;
};

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
                                        const NetworkFilterFactoriesList& filter_factories) PURE;

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

  /**
   * Called to create the QUIC listener filter chain.
   * @param manager supplies the filter manager to create the chain on.
   * @return true if filter chain was created successfully. Otherwise false.
   */
  virtual bool createQuicListenerFilterChain(QuicListenerFilterManager& manager) PURE;
};

/**
 * Network filter matching context data for unified matchers.
 */
class MatchingData {
public:
  static absl::string_view name() { return "network"; }

  virtual ~MatchingData() = default;

  virtual const ConnectionSocket& socket() const PURE;
  virtual const StreamInfo::FilterState& filterState() const PURE;
  virtual const envoy::config::core::v3::Metadata& dynamicMetadata() const PURE;

  const ConnectionInfoProvider& connectionInfoProvider() const {
    return socket().connectionInfoProvider();
  }

  const Address::Instance& localAddress() const { return *connectionInfoProvider().localAddress(); }

  const Address::Instance& remoteAddress() const {
    return *connectionInfoProvider().remoteAddress();
  }

  Ssl::ConnectionInfoConstSharedPtr ssl() const { return connectionInfoProvider().sslConnection(); }
};

/**
 * UDP listener filter matching context data for unified matchers.
 */
class UdpMatchingData {
public:
  static absl::string_view name() { return "network"; }

  virtual ~UdpMatchingData() = default;

  virtual const Address::Instance& localAddress() const PURE;
  virtual const Address::Instance& remoteAddress() const PURE;
};

} // namespace Network
} // namespace Envoy
