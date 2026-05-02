#pragma once

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

enum class NetworkFilterStatus : uint32_t {
  Continue = 0,
  Stop = 1,
};

enum class NetworkConnectionCloseType : uint32_t {
  FlushWrite,
  NoFlush,
  FlushWriteAndDelay,
  Abort,
  AbortReset,
};

enum class NetworkConnectionEvent : uint32_t {
  RemoteClose,
  LocalClose,
  Connected,
  ConnectedZeroRtt,
};

enum class NetworkConnectionState : uint32_t {
  Open,
  Closing,
  Closed,
};

enum class NetworkReadDisableStatus : uint32_t {
  NoTransition,
  StillReadDisabled,
  TransitionedToReadEnabled,
  TransitionedToReadDisabled,
};

enum class SocketOptionValueType : uint32_t { Int, Bytes };

struct SocketOption {
  int64_t level;
  int64_t name;
  SocketOptionState state;
  SocketOptionValueType value_type;
  int64_t int_value;
  BufferView byte_value;
};

class NetworkBuffer {
public:
  virtual ~NetworkBuffer();

  /**
   * Returns the current buffer contents as Envoy-owned chunks.
   * Copy the data if it must outlive the current callback.
   */
  virtual std::vector<BufferView> getChunks() const = 0;

  /**
   * Returns the total size of the current buffer in bytes.
   */
  virtual size_t getSize() const = 0;

  /**
   * Removes bytes from the front of the buffer.
   * @return true if the operation succeeds.
   */
  virtual bool drain(size_t size) = 0;

  /**
   * Prepends data to the front of the buffer.
   * @return true if the operation succeeds.
   */
  virtual bool prepend(std::string_view data) = 0;

  /**
   * Appends data to the end of the buffer.
   * @return true if the operation succeeds.
   */
  virtual bool append(std::string_view data) = 0;
};

class NetworkFilterHandle {
public:
  virtual ~NetworkFilterHandle();

  /** Returns the current downstream -> upstream buffer. */
  virtual NetworkBuffer& readBuffer() = 0;
  /** Returns the current upstream -> downstream buffer. */
  virtual NetworkBuffer& writeBuffer() = 0;

  /**
   * Writes data directly to the downstream connection.
   * @param data The bytes to write.
   * @param end_stream If true, half-closes after the write.
   */
  virtual void write(std::string_view data, bool end_stream) = 0;

  /**
   * Injects data into the read filter chain after this filter.
   * @param data The bytes to inject.
   * @param end_stream Whether this is the final read-side data.
   */
  virtual void injectReadData(std::string_view data, bool end_stream) = 0;

  /**
   * Injects data into the write filter chain after this filter.
   * @param data The bytes to inject.
   * @param end_stream Whether this is the final write-side data.
   */
  virtual void injectWriteData(std::string_view data, bool end_stream) = 0;

  /** Resumes iteration after a previous Stop result. */
  virtual void continueReading() = 0;

  /** Closes the connection using the supplied close behavior. */
  virtual void close(NetworkConnectionCloseType close_type) = 0;

  /** Returns Envoy's unique ID for this connection. */
  virtual uint64_t getConnectionId() = 0;

  /** Returns the remote address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getRemoteAddress() = 0;

  /** Returns the local address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getLocalAddress() = 0;

  /** Returns true when the connection is using SSL/TLS. */
  virtual bool isSsl() = 0;

  /** Disables or re-enables close handling for this filter instance. */
  virtual void disableClose(bool disabled) = 0;

  /** Closes the connection and records termination details. */
  virtual void closeWithDetails(NetworkConnectionCloseType close_type,
                                std::string_view details) = 0;

  /** Returns the requested server name (SNI) if present. */
  virtual std::optional<std::string_view> getRequestedServerName() = 0;

  /** Returns the direct remote address and port without proxy/XFF handling. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getDirectRemoteAddress() = 0;

  /** Returns the peer certificate URI SANs. */
  virtual std::vector<std::string_view> getSslUriSans() = 0;

  /** Returns the peer certificate DNS SANs. */
  virtual std::vector<std::string_view> getSslDnsSans() = 0;

  /** Returns the peer certificate subject if available. */
  virtual std::optional<std::string_view> getSslSubject() = 0;

  /** Stores a raw bytes filter state value. */
  virtual bool setFilterState(std::string_view key, std::string_view value) = 0;

  /** Returns a raw bytes filter state value if available. */
  virtual std::optional<std::string_view> getFilterState(std::string_view key) = 0;

  /** Stores a typed filter state value using Envoy's ObjectFactory for the key. */
  virtual bool setFilterStateTyped(std::string_view key, std::string_view value) = 0;

  /** Returns the serialized bytes of a typed filter state value if available. */
  virtual std::optional<std::string_view> getFilterStateTyped(std::string_view key) = 0;

  /** Returns a dynamic metadata string value if available. */
  virtual std::optional<std::string_view> getMetadataString(std::string_view ns,
                                                            std::string_view key) = 0;

  /** Returns a dynamic metadata numeric value if available. */
  virtual std::optional<double> getMetadataNumber(std::string_view ns, std::string_view key) = 0;

  /** Returns a dynamic metadata bool value if available. */
  virtual std::optional<bool> getMetadataBool(std::string_view ns, std::string_view key) = 0;

  /** Sets a string dynamic metadata value, replacing any existing value. */
  virtual void setMetadata(std::string_view ns, std::string_view key, std::string_view value) = 0;

  /** Sets a numeric dynamic metadata value, replacing any existing value. */
  virtual void setMetadata(std::string_view ns, std::string_view key, double value) = 0;

  /** Sets a bool dynamic metadata value, replacing any existing value. */
  virtual void setMetadata(std::string_view ns, std::string_view key, bool value) = 0;
  void setMetadata(std::string_view ns, std::string_view key, const char* value) {
    setMetadata(ns, key, std::string_view(value));
  }

  /** Stores an integer socket option for the supplied socket state. */
  virtual void setSocketOptionInt(int64_t level, int64_t name, SocketOptionState state,
                                  int64_t value) = 0;

  /** Stores a bytes socket option for the supplied socket state. */
  virtual void setSocketOptionBytes(int64_t level, int64_t name, SocketOptionState state,
                                    std::string_view value) = 0;

  /** Returns an integer socket option value if one is present. */
  virtual std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name,
                                                    SocketOptionState state) = 0;

  /** Returns a bytes socket option value if one is present. */
  virtual std::optional<std::string_view> getSocketOptionBytes(int64_t level, int64_t name,
                                                               SocketOptionState state) = 0;

  /** Returns all socket options currently stored on the connection. */
  virtual std::vector<SocketOption> getSocketOptions() = 0;

  /**
   * Initiates an asynchronous HTTP callout from this network filter.
   * The callback must remain valid until completion.
   */
  virtual std::pair<HttpCalloutInitResult, uint64_t>
  httpCallout(std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
              uint64_t timeout_ms, HttpCalloutCallback& cb) = 0;

  /** Records a value in a histogram metric defined from the config handle. */
  virtual MetricsResult recordHistogramValue(MetricID id, uint64_t value) = 0;

  /** Sets a gauge metric to the supplied value. */
  virtual MetricsResult setGaugeValue(MetricID id, uint64_t value) = 0;

  /** Increments a gauge metric by the supplied value. */
  virtual MetricsResult incrementGaugeValue(MetricID id, uint64_t value) = 0;

  /** Decrements a gauge metric by the supplied value. */
  virtual MetricsResult decrementGaugeValue(MetricID id, uint64_t value) = 0;

  /** Increments a counter metric by the supplied value. */
  virtual MetricsResult incrementCounterValue(MetricID id, uint64_t value) = 0;

  /** Returns host counts for the supplied cluster and priority if available. */
  virtual std::optional<ClusterHostCounts> getClusterHostCounts(std::string_view cluster,
                                                                uint32_t priority) = 0;

  /** Returns the selected upstream host address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getUpstreamHostAddress() = 0;

  /** Returns the selected upstream host hostname if available. */
  virtual std::optional<std::string_view> getUpstreamHostHostname() = 0;

  /** Returns the selected upstream host cluster name if available. */
  virtual std::optional<std::string_view> getUpstreamHostCluster() = 0;

  /** Returns true when an upstream host has been selected. */
  virtual bool hasUpstreamHost() = 0;

  /** Attempts to promote the upstream transport into secure mode. */
  virtual bool startUpstreamSecureTransport() = 0;

  /** Returns the current connection state. */
  virtual NetworkConnectionState getConnectionState() = 0;

  /** Disables or re-enables reading, returning Envoy's transition status. */
  virtual NetworkReadDisableStatus readDisable(bool disable) = 0;

  /** Returns whether reading is currently enabled. */
  virtual bool readEnabled() = 0;

  /** Returns whether half-close semantics are enabled. */
  virtual bool isHalfCloseEnabled() = 0;

  /** Enables or disables half-close semantics. */
  virtual void enableHalfClose(bool enabled) = 0;

  /** Returns the current soft buffer limit in bytes. */
  virtual uint32_t getBufferLimit() = 0;

  /** Updates the soft buffer limit in bytes. */
  virtual void setBufferLimits(uint32_t limit) = 0;

  /** Returns true when the write buffer is currently above the high watermark. */
  virtual bool aboveHighWatermark() = 0;

  /**
   * Returns a scheduler tied to this filter's worker thread.
   * The scheduler must be acquired during a filter callback and may be used later from other
   * threads.
   */
  virtual std::shared_ptr<Scheduler> getScheduler() = 0;

  /** Returns the worker index assigned to this filter instance. */
  virtual uint32_t getWorkerIndex() = 0;

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

class NetworkFilterConfigHandle {
public:
  virtual ~NetworkFilterConfigHandle();

  /** Defines a histogram metric during config initialization. */
  virtual std::pair<MetricID, MetricsResult> defineHistogram(std::string_view name) = 0;

  /** Defines a gauge metric during config initialization. */
  virtual std::pair<MetricID, MetricsResult> defineGauge(std::string_view name) = 0;

  /** Defines a counter metric during config initialization. */
  virtual std::pair<MetricID, MetricsResult> defineCounter(std::string_view name) = 0;

  /**
   * Returns a scheduler tied to the main-thread config context.
   * It must be acquired during config creation and may be used later from other threads.
   */
  virtual std::shared_ptr<Scheduler> getScheduler() = 0;

  /** Returns whether Envoy logging is enabled for the supplied level. */
  virtual bool logEnabled(LogLevel level) = 0;

  /** Logs a message through Envoy's logging subsystem. */
  virtual void log(LogLevel level, std::string_view message) = 0;
};

class NetworkFilter {
public:
  virtual ~NetworkFilter();

  /** Called when a new TCP connection has been established. */
  virtual NetworkFilterStatus onNewConnection() = 0;

  /** Called for downstream -> upstream data. */
  virtual NetworkFilterStatus onRead(NetworkBuffer& data, bool end_stream) = 0;

  /** Called for upstream -> downstream data. */
  virtual NetworkFilterStatus onWrite(NetworkBuffer& data, bool end_stream) = 0;

  /** Called for connection lifecycle events such as connect and close. */
  virtual void onEvent(NetworkConnectionEvent event) = 0;

  /** Called when the filter is being destroyed. */
  virtual void onDestroy() = 0;

  /** Called when the write buffer crosses above the configured high watermark. */
  virtual void onAboveWriteBufferHighWatermark() {}

  /** Called when the write buffer drops below the low watermark after being above high watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() {}
};

class NetworkFilterFactory {
public:
  virtual ~NetworkFilterFactory();

  /** Creates the per-connection network filter instance. */
  virtual std::unique_ptr<NetworkFilter> create(NetworkFilterHandle& handle) = 0;

  /** Called when the factory is being destroyed. */
  virtual void onDestroy() {}
};

class NetworkFilterConfigFactory {
public:
  virtual ~NetworkFilterConfigFactory();

  /** Parses config_view and returns the thread-safe factory used for subsequent connections. */
  virtual std::unique_ptr<NetworkFilterFactory> create(NetworkFilterConfigHandle& handle,
                                                       std::string_view config_view) = 0;
};

using NetworkFilterConfigFactoryPtr = std::unique_ptr<NetworkFilterConfigFactory>;

class NetworkFilterConfigFactoryRegistry {
public:
  static const std::map<std::string_view, NetworkFilterConfigFactoryPtr>& getRegistry();

private:
  static std::map<std::string_view, NetworkFilterConfigFactoryPtr>& getMutableRegistry();
  friend class NetworkFilterConfigFactoryRegister;
};

class NetworkFilterConfigFactoryRegister {
public:
  NetworkFilterConfigFactoryRegister(std::string_view name, NetworkFilterConfigFactoryPtr factory);
  ~NetworkFilterConfigFactoryRegister();

private:
  const std::string name_;
};

#define REGISTER_NETWORK_FILTER_CONFIG_FACTORY(FACTORY_CLASS, NAME)                                \
  static Envoy::DynamicModules::NetworkFilterConfigFactoryRegister                                 \
      NetworkFilterConfigFactoryRegister_##FACTORY_CLASS##_register_NAME(                          \
          NAME, std::unique_ptr<Envoy::DynamicModules::NetworkFilterConfigFactory>(                \
                    new FACTORY_CLASS()));

} // namespace DynamicModules
} // namespace Envoy
