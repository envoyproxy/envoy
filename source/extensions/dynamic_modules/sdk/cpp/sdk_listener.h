#pragma once

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

/** Result returned by listener filter callbacks. */
enum class ListenerFilterStatus : uint32_t {
  Continue = 0,
  StopIteration = 1,
};

/** Type of local address currently associated with the downstream socket. */
enum class AddressType : uint32_t {
  Unknown = 0,
  Ip = 1,
  Pipe = 2,
  EnvoyInternal = 3,
};

/** Host interface exposed to per-connection listener filter instances. */
class ListenerFilterHandle {
public:
  virtual ~ListenerFilterHandle();

  /** Returns the current listener-filter peek buffer chunk if one is available. */
  virtual std::optional<BufferView> getBufferChunk() = 0;

  /** Drains bytes from the listener-filter peek buffer. */
  virtual bool drainBuffer(size_t length) = 0;

  /** Overrides the detected transport protocol. */
  virtual void setDetectedTransportProtocol(std::string_view protocol) = 0;

  /** Overrides the requested server name. */
  virtual void setRequestedServerName(std::string_view name) = 0;

  /** Overrides the requested application protocols (ALPN). */
  virtual void setRequestedApplicationProtocols(std::span<const BufferView> protocols) = 0;

  /** Overrides the `JA3` TLS fingerprint. */
  virtual void setJa3Hash(std::string_view hash) = 0;

  /** Overrides the `JA4` TLS fingerprint. */
  virtual void setJa4Hash(std::string_view hash) = 0;

  /** Returns the requested server name (SNI) if present. */
  virtual std::optional<std::string_view> getRequestedServerName() = 0;

  /** Returns the detected transport protocol if present. */
  virtual std::optional<std::string_view> getDetectedTransportProtocol() = 0;

  /** Returns the requested application protocols (ALPN). */
  virtual std::vector<std::string_view> getRequestedApplicationProtocols() = 0;

  /** Returns the `JA3` TLS fingerprint if present. */
  virtual std::optional<std::string_view> getJa3Hash() = 0;

  /** Returns the `JA4` TLS fingerprint if present. */
  virtual std::optional<std::string_view> getJa4Hash() = 0;

  /** Returns true when the downstream connection is using SSL/TLS. */
  virtual bool isSsl() = 0;

  /** Returns the peer certificate URI SANs. */
  virtual std::vector<std::string_view> getSslUriSans() = 0;

  /** Returns the peer certificate DNS SANs. */
  virtual std::vector<std::string_view> getSslDnsSans() = 0;

  /** Returns the peer certificate subject if present. */
  virtual std::optional<std::string_view> getSslSubject() = 0;

  /** Returns the effective remote address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getRemoteAddress() = 0;

  /** Returns the direct remote address and port without proxy/XFF handling. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getDirectRemoteAddress() = 0;

  /** Returns the effective local address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getLocalAddress() = 0;

  /** Returns the direct local address and port before restoration. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getDirectLocalAddress() = 0;

  /** Returns the original destination address and port if available. */
  virtual std::optional<std::pair<std::string_view, uint32_t>> getOriginalDst() = 0;

  /** Returns the current local address type. */
  virtual AddressType getAddressType() = 0;

  /** Returns whether Envoy restored the local address. */
  virtual bool isLocalAddressRestored() = 0;

  /** Overrides the effective remote address. */
  virtual bool setRemoteAddress(std::string_view address, uint32_t port, bool is_ipv6) = 0;

  /** Restores the local address to the supplied value. */
  virtual bool restoreLocalAddress(std::string_view address, uint32_t port, bool is_ipv6) = 0;

  /** Resumes listener filter iteration after a previous stop result. */
  virtual void continueFilterChain(bool success) = 0;

  /** Enables or disables use of the original destination address. */
  virtual void useOriginalDst(bool use_original_dst) = 0;

  /** Closes the downstream socket and records optional details. */
  virtual void closeSocket(std::string_view details) = 0;

  /** Closes the downstream socket without additional details. */
  void closeSocket() { closeSocket({}); }

  /** Writes bytes directly to the downstream socket. */
  virtual int64_t writeToSocket(std::string_view data) = 0;

  /** Returns the socket file descriptor when Envoy exposes one. */
  virtual int64_t getSocketFd() = 0;

  /** Sets an integer socket option on the downstream socket. */
  virtual bool setSocketOptionInt(int64_t level, int64_t name, int64_t value) = 0;

  /** Sets a bytes socket option on the downstream socket. */
  virtual bool setSocketOptionBytes(int64_t level, int64_t name, std::string_view value) = 0;

  /** Returns an integer socket option value if one is present. */
  virtual std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name) = 0;

  /** Returns a bytes socket option value if one is present. */
  virtual std::optional<std::vector<uint8_t>> getSocketOptionBytes(int64_t level, int64_t name,
                                                                   size_t max_size) = 0;

  /** Sets a string dynamic metadata value. */
  virtual void setDynamicMetadataString(std::string_view ns, std::string_view key,
                                        std::string_view value) = 0;

  /** Returns a string dynamic metadata value if one is present. */
  virtual std::optional<std::string_view> getDynamicMetadataString(std::string_view ns,
                                                                   std::string_view key) = 0;

  /** Sets a numeric dynamic metadata value. */
  virtual void setDynamicMetadataNumber(std::string_view ns, std::string_view key,
                                        double value) = 0;

  /** Returns a numeric dynamic metadata value if one is present. */
  virtual std::optional<double> getDynamicMetadataNumber(std::string_view ns,
                                                         std::string_view key) = 0;

  /** Stores a raw bytes filter state value. */
  virtual bool setFilterState(std::string_view key, std::string_view value) = 0;

  /** Returns a raw bytes filter state value if one is present. */
  virtual std::optional<std::string_view> getFilterState(std::string_view key) = 0;

  /** Records the downstream transport failure reason. */
  virtual void setDownstreamTransportFailureReason(std::string_view reason) = 0;

  /** Returns the downstream connection start time in milliseconds since epoch. */
  virtual uint64_t getConnectionStartTimeMs() = 0;

  /** Returns Envoy's current listener-filter max read size in bytes. */
  virtual size_t currentMaxReadBytes() = 0;

  /** Initiates an asynchronous HTTP callout from this listener filter. */
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

/** Host interface exposed while creating a thread-safe listener filter factory. */
class ListenerFilterConfigHandle {
public:
  virtual ~ListenerFilterConfigHandle();

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

/** Base class for listener filters that operate on newly accepted downstream sockets. */
class ListenerFilter {
public:
  virtual ~ListenerFilter();

  /** Called when Envoy accepts a new downstream socket. */
  virtual ListenerFilterStatus onAccept() { return ListenerFilterStatus::Continue; }

  /** Called when bytes are available in the listener-filter peek buffer. */
  virtual ListenerFilterStatus onData(size_t) { return ListenerFilterStatus::Continue; }

  /** Called when the socket closes while this filter owns iteration. */
  virtual void onClose() {}

  /** Called when the filter instance is being destroyed. */
  virtual void onDestroy() {}

  /** Returns how many bytes Envoy should peek for this filter. */
  virtual size_t maxReadBytes() { return 0; }
};

/** Factory interface that creates per-connection listener filters. */
class ListenerFilterFactory {
public:
  virtual ~ListenerFilterFactory();

  /** Creates the per-connection listener filter instance. */
  virtual std::unique_ptr<ListenerFilter> create(ListenerFilterHandle& handle) = 0;

  /** Called when the factory is being destroyed. */
  virtual void onDestroy() {}
};

/** Factory interface that parses config and creates thread-safe listener filter factories. */
class ListenerFilterConfigFactory {
public:
  virtual ~ListenerFilterConfigFactory();

  /** Parses config_view and returns the thread-safe factory used for subsequent sockets. */
  virtual std::unique_ptr<ListenerFilterFactory> create(ListenerFilterConfigHandle& handle,
                                                        std::string_view config_view) = 0;
};

/** Unique pointer alias for listener filter config factories stored in the registry. */
using ListenerFilterConfigFactoryPtr = std::unique_ptr<ListenerFilterConfigFactory>;

/** Registry of statically registered listener filter config factories. */
class ListenerFilterConfigFactoryRegistry {
public:
  /** Returns the registered listener filter config factories keyed by name. */
  static const std::map<std::string_view, ListenerFilterConfigFactoryPtr>& getRegistry();

private:
  static std::map<std::string_view, ListenerFilterConfigFactoryPtr>& getMutableRegistry();
  friend class ListenerFilterConfigFactoryRegister;
};

/** RAII helper that inserts and removes a listener filter config factory registration. */
class ListenerFilterConfigFactoryRegister {
public:
  /** Registers a listener filter config factory under name for the binary lifetime. */
  ListenerFilterConfigFactoryRegister(std::string_view name,
                                      ListenerFilterConfigFactoryPtr factory);
  ~ListenerFilterConfigFactoryRegister();

private:
  const std::string name_;
};

/** Registers a listener filter config factory during static initialization. */
#define REGISTER_LISTENER_FILTER_CONFIG_FACTORY(FACTORY_CLASS, NAME)                               \
  static Envoy::DynamicModules::ListenerFilterConfigFactoryRegister                                \
      ListenerFilterConfigFactoryRegister_##FACTORY_CLASS##_register_NAME(                         \
          NAME, std::unique_ptr<Envoy::DynamicModules::ListenerFilterConfigFactory>(               \
                    new FACTORY_CLASS()));

} // namespace DynamicModules
} // namespace Envoy
