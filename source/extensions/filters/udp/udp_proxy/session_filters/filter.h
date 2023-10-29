#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/listener.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

/**
 * Common interface for ReadFilterCallbacks and WriteFilterCallbacks.
 */
class FilterCallbacks {
public:
  virtual ~FilterCallbacks() = default;

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

class ReadFilterCallbacks : public FilterCallbacks {
public:
  ~ReadFilterCallbacks() override = default;

  /**
   * If a read filter stopped filter iteration, continueFilterChain() can be called to continue the
   * filter chain. It will have onNewSession() called if it was not previously called.
   * @return false if the session is removed and no longer valid, otherwise returns true.
   */
  virtual bool continueFilterChain() PURE;
};

class WriteFilterCallbacks : public FilterCallbacks {};

/**
 * Return codes for read filter invocations.
 */
enum class ReadFilterStatus {
  // Continue to further session filters.
  Continue,
  // Stop executing further session filters.
  StopIteration,
};

/**
 * Session read filter interface.
 */
class ReadFilter {
public:
  virtual ~ReadFilter() = default;

  /**
   * Called when a new UDP session is first established. Filters should do one time long term
   * processing that needs to be done when a session is established. Filter chain iteration
   * can be stopped if needed.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual ReadFilterStatus onNewSession() PURE;

  /**
   * Called when UDP datagram is read and matches the session that manages the filter.
   * @param data supplies the read data which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual ReadFilterStatus onData(Network::UdpRecvData& data) PURE;

  /**
   * Initializes the read filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of onNewSession() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) PURE;
};

using ReadFilterSharedPtr = std::shared_ptr<ReadFilter>;

/**
 * Return codes for write filter invocations.
 */
enum class WriteFilterStatus {
  // Continue to further session filters.
  Continue,
  // Stop executing further session filters.
  StopIteration,
};

/**
 * Session write filter interface.
 */
class WriteFilter {
public:
  virtual ~WriteFilter() = default;

  /**
   * Called when data is to be written on the UDP session.
   * @param data supplies the buffer to be written which may be modified.
   * @return status used by the filter manager to manage further filter iteration.
   */
  virtual WriteFilterStatus onWrite(Network::UdpRecvData& data) PURE;

  /**
   * Initializes the write filter callbacks used to interact with the filter manager. It will be
   * called by the filter manager a single time when the filter is first registered.
   *
   * IMPORTANT: No outbound networking or complex processing should be done in this function.
   *            That should be done in the context of ReadFilter::onNewSession() if needed.
   *
   * @param callbacks supplies the callbacks.
   */
  virtual void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) PURE;
};

using WriteFilterSharedPtr = std::shared_ptr<WriteFilter>;

/**
 * A combination read and write filter. This allows a single filter instance to cover
 * both the read and write paths.
 */
class Filter : public virtual ReadFilter, public virtual WriteFilter {};
using FilterSharedPtr = std::shared_ptr<Filter>;

/**
 * These callbacks are provided by the UDP session manager to the factory so that the factory
 * can * build the filter chain in an application specific way.
 */
class FilterChainFactoryCallbacks {
public:
  virtual ~FilterChainFactoryCallbacks() = default;

  /**
   * Add a read filter that is used when reading UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addReadFilter(ReadFilterSharedPtr filter) PURE;

  /**
   * Add a write filter that is used when writing UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addWriteFilter(WriteFilterSharedPtr filter) PURE;

  /**
   * Add a bidirectional filter that is used when reading and writing UDP session data.
   * @param filter supplies the filter to add.
   */
  virtual void addFilter(FilterSharedPtr filter) PURE;
};

/**
 * This function is used to wrap the creation of a UDP session filter chain for new sessions as they
 * come in. Filter factories create the function at configuration initialization time, and then
 * they are used at runtime.
 * @param callbacks supplies the callbacks for the stream to install filters to. Typically the
 * function will install a single filter, but it's technically possibly to install more than one
 * if desired.
 */
using FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks& callbacks)>;

/**
 * A FilterChainFactory is used by a UDP session manager to create a UDP session filter chain when
 * a new session is created.
 */
class FilterChainFactory {
public:
  virtual ~FilterChainFactory() = default;

  /**
   * Called when a new UDP session is created.
   * @param callbacks supplies the "sink" that is used for actually creating the filter chain. @see
   *                  FilterChainFactoryCallbacks.
   */
  virtual void createFilterChain(FilterChainFactoryCallbacks& callbacks) const PURE;
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
