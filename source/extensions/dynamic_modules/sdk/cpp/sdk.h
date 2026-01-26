#pragma once

#include <cassert>
#include <cstddef>
#include <memory>

#include "absl/container/flat_hash_map.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"

namespace Envoy {
namespace DynamicModules {

/** Interface definitions for the dynamic module SDK. */

/**
 * BufferView should have the same layout as envoy_dynamic_module_type_module_buffer
 * and envoy_dynamic_module_type_envoy_buffer.
 */
class BufferView {
public:
  /**
   * Constructs a BufferView from a string_view.
   * @param data The string view to construct from.
   */
  BufferView(absl::string_view data) : data_(data.data()), length_(data.size()) {}
  BufferView(const char* data, size_t length) : data_(data), length_(length) {}
  /**
   * Default constructor.
   */
  BufferView() = default;

  /**
   * Returns the buffer as a string_view.
   * @return The buffer contents as a string_view.
   */
  absl::string_view toStringView() const { return {data_, length_}; }

  size_t size() const { return length_; }
  const char* data() const { return data_; }

private:
  const char* data_{};
  size_t length_{};
};

/**
 * HeaderView should have the same layout as envoy_dynamic_module_type_module_http_header
 * and envoy_dynamic_module_type_envoy_http_header.
 */
class HeaderView {
public:
  /**
   * Constructs a HeaderView from key and value string_views.
   * @param key The header key.
   * @param value The header value.
   */
  HeaderView(absl::string_view key, absl::string_view value)
      : key_data_(key.data()), key_length_(key.size()), value_data_(value.data()),
        value_length_(value.size()) {}
  HeaderView(const char* key_data, size_t key_length, const char* value_data, size_t value_length)
      : key_data_(key_data), key_length_(key_length), value_data_(value_data),
        value_length_(value_length) {}
  /**
   * Default constructor.
   */
  HeaderView() = default;

  /**
   * Returns the header key as a string_view.
   * @return The header key.
   */
  absl::string_view key() const { return {key_data_, key_length_}; }

  /**
   * Returns the header value as a string_view.
   * @return The header value.
   */
  absl::string_view value() const { return {value_data_, value_length_}; }

  const char* keyData() const { return key_data_; }
  size_t keyLength() const { return key_length_; }
  const char* valueData() const { return value_data_; }
  size_t valueLength() const { return value_length_; }

private:
  const char* key_data_{};
  size_t key_length_{};
  const char* value_data_{};
  size_t value_length_{};
};

/** BodyBuffer interface */
class BodyBuffer {
public:
  virtual ~BodyBuffer();

  /**
   * Returns all data chunks in the buffer.
   * @return Vector of buffer views containing all chunks.
   */
  virtual std::vector<BufferView> getChunks() = 0;

  /**
   * Returns the total size of the buffer.
   * @return The total size in bytes.
   */
  virtual size_t getSize() = 0;

  /**
   * Removes size from the front of the buffer.
   * @param size Number of bytes to remove.
   */
  virtual void drain(size_t size) = 0;

  /**
   * Appends data to the buffer.
   * @param data The data to append.
   */
  virtual void append(absl::string_view data) = 0;
};

/** HeaderMap interface */
class HeaderMap {
public:
  virtual ~HeaderMap();

  /**
   * Returns all values for a given header key.
   * @param key The header key to look up.
   * @return Vector of string views containing all values for the key.
   */
  virtual std::vector<absl::string_view> get(absl::string_view key) const = 0;

  /**
   * Returns the first value for a given header key.
   * @param key The header key to look up.
   * @return The first header value, or empty if not found.
   */
  virtual absl::string_view getOne(absl::string_view key) const = 0;

  /**
   * Returns all headers.
   * @return Vector of HeaderView containing all headers.
   */
  virtual std::vector<HeaderView> getAll() const = 0;

  /**
   * Return size of the header map.
   * @return The number of headers in the map.
   */
  virtual size_t size() const = 0;

  /**
   * Sets a header key-value pair (replaces existing).
   * @param key The header key.
   * @param value The header value.
   */
  virtual void set(absl::string_view key, absl::string_view value) = 0;

  /**
   * Adds a header key-value pair (appends to existing).
   * @param key The header key.
   * @param value The header value.
   */
  virtual void add(absl::string_view key, absl::string_view value) = 0;

  /**
   * Removes all headers with the given key.
   * @param key The header key to remove.
   */
  virtual void remove(absl::string_view key) = 0;
};

/** Attribute IDs */
enum class AttributeID : uint32_t {
  RequestPath = 0,
  RequestUrlPath,
  RequestHost,
  RequestScheme,
  RequestMethod,
  RequestHeaders,
  RequestReferer,
  RequestUserAgent,
  RequestTime,
  RequestId,
  RequestProtocol,
  RequestQuery,
  RequestDuration,
  RequestSize,
  RequestTotalSize,
  ResponseCode,
  ResponseCodeDetails,
  ResponseFlags,
  ResponseGrpcStatus,
  ResponseHeaders,
  ResponseTrailers,
  ResponseSize,
  ResponseTotalSize,
  ResponseBackendLatency,
  SourceAddress,
  SourcePort,
  DestinationAddress,
  DestinationPort,
  ConnectionId,
  ConnectionMtls,
  ConnectionRequestedServerName,
  ConnectionTlsVersion,
  ConnectionSubjectLocalCertificate,
  ConnectionSubjectPeerCertificate,
  ConnectionDnsSanLocalCertificate,
  ConnectionDnsSanPeerCertificate,
  ConnectionUriSanLocalCertificate,
  ConnectionUriSanPeerCertificate,
  ConnectionSha256PeerCertificateDigest,
  ConnectionTransportFailureReason,
  ConnectionTerminationDetails,
  UpstreamAddress,
  UpstreamPort,
  UpstreamTlsVersion,
  UpstreamSubjectLocalCertificate,
  UpstreamSubjectPeerCertificate,
  UpstreamDnsSanLocalCertificate,
  UpstreamDnsSanPeerCertificate,
  UpstreamUriSanLocalCertificate,
  UpstreamUriSanPeerCertificate,
  UpstreamSha256PeerCertificateDigest,
  UpstreamLocalAddress,
  UpstreamTransportFailureReason,
  UpstreamRequestAttemptCount,
  UpstreamCxPoolReadyDuration,
  UpstreamLocality,
  XdsNode,
  XdsClusterName,
  XdsClusterMetadata,
  XdsListenerDirection,
  XdsListenerMetadata,
  XdsRouteName,
  XdsRouteMetadata,
  XdsVirtualHostName,
  XdsVirtualHostMetadata,
  XdsUpstreamHostMetadata,
  XdsFilterChainName
};

enum class LogLevel : uint32_t { Trace, Debug, Info, Warn, Error, Critical, Off };

enum class HttpCalloutInitResult : uint32_t {
  Success,
  MissingRequiredHeaders,
  ClusterNotFound,
  DuplicateCalloutId,
  CannotCreateRequest
};

enum class HttpCalloutResult : uint32_t { Success, Reset, ExceedResponseBufferLimit };

class HttpCalloutCallback {
public:
  virtual ~HttpCalloutCallback();

  /**
   * Invokes the callback with the HTTP callout result, headers, and body chunks.
   * @param result The HTTP callout result status.
   * @param headers Span of response headers.
   * @param body_chunks Span of response body chunks.
   */
  virtual void onHttpCalloutDone(HttpCalloutResult result, absl::Span<const HeaderView> headers,
                                 absl::Span<const BufferView> body_chunks) = 0;
};

enum class HttpStreamResetReason : uint32_t {
  ConnectionFailure,
  ConnectionTermination,
  LocalReset,
  LocalRefusedStreamReset,
  Overflow,
  RemoteReset,
  RemoteRefusedStreamReset,
  ProtocolError,
};

class HttpStreamCallback {
public:
  virtual ~HttpStreamCallback();

  /**
   * Called when response headers are received.
   * @param stream_id The ID of the stream.
   * @param headers The response headers.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void onHttpStreamHeaders(uint64_t stream_id, absl::Span<const HeaderView> headers,
                                   bool end_stream) = 0;

  /**
   * Called when response data is received.
   * @param stream_id The ID of the stream.
   * @param body The response body chunks.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void onHttpStreamData(uint64_t stream_id, absl::Span<const BufferView> body,
                                bool end_stream) = 0;

  /**
   * Called when response trailers are received.
   * @param stream_id The ID of the stream.
   * @param trailers The response trailers.
   */
  virtual void onHttpStreamTrailers(uint64_t stream_id, absl::Span<const HeaderView> trailers) = 0;

  /**
   * Called when the stream is complete.
   * @param stream_id The ID of the stream.
   */
  virtual void onHttpStreamComplete(uint64_t stream_id) = 0;

  /**
   * Called when the stream is reset.
   * @param stream_id The ID of the stream.
   * @param reason The reason for the reset.
   */
  virtual void onHttpStreamReset(uint64_t stream_id, HttpStreamResetReason reason) = 0;
};

class RouteSpecificConfig {
public:
  virtual ~RouteSpecificConfig();
};

class Scheduler {
public:
  virtual ~Scheduler();

  /**
   * Schedules a function for deferred execution.
   * @param func The function to schedule.
   */
  virtual void schedule(std::function<void()> func) = 0;
};

class DownstreamWatermarkCallbacks {
public:
  virtual ~DownstreamWatermarkCallbacks();

  /**
   * Called when the downstream write buffer exceeds the high watermark.
   */
  virtual void onAboveWriteBufferHighWatermark() = 0;

  /**
   * Called when the downstream write buffer drops below the low watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() = 0;
};

using MetricID = uint64_t;
enum class MetricsResult : uint32_t { Success, NotFound, InvalidTags, Frozen };

class HttpFilterHandle {
public:
  virtual ~HttpFilterHandle();

  /**
   * Retrieves a string metadata value by namespace and key.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @return Pair of string view and bool indicating if value was found.
   */
  virtual absl::optional<absl::string_view> getMetadataString(absl::string_view ns,
                                                              absl::string_view key) = 0;

  /**
   * Retrieves a numeric metadata value by namespace and key.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @return Pair of double and bool indicating if value was found.
   */
  virtual absl::optional<double> getMetadataNumber(absl::string_view ns, absl::string_view key) = 0;

  /**
   * Sets a string metadata value.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The string value to set.
   */
  virtual void setMetadata(absl::string_view ns, absl::string_view key,
                           absl::string_view value) = 0;

  /**
   * Sets a numeric metadata value.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The numeric value to set.
   */
  virtual void setMetadata(absl::string_view ns, absl::string_view key, double value) = 0;

  /**
   * Retrieves the serialized filter state value of the stream.
   * @param key The filter state key.
   * @return The filter state value if found, otherwise empty.
   */
  virtual absl::optional<absl::string_view> getFilterState(absl::string_view key) = 0;

  /**
   * Sets the serialized filter state value of the stream.
   * @param key The filter state key.
   * @param value The filter state value.
   */
  virtual void setFilterState(absl::string_view key, absl::string_view value) = 0;

  /**
   * Retrieves a string attribute value.
   * @param id The attribute ID.
   * @return Pair of string view and bool indicating if attribute was found.
   */
  virtual absl::optional<absl::string_view> getAttributeString(AttributeID id) = 0;

  /**
   * Retrieves a numeric attribute value.
   * @param id The attribute ID.
   * @return Pair of double and bool indicating if attribute was found.
   */
  virtual absl::optional<uint64_t> getAttributeNumber(AttributeID id) = 0;

  /**
   * Sends a local response with status code, body, and detail.
   * @param status The HTTP status code.
   * @param body The response body.
   * @param detail The response detail.
   */
  virtual void sendLocalResponse(uint32_t status, absl::Span<const HeaderView> headers,
                                 absl::string_view body, absl::string_view detail) = 0;

  /**
   * Sends response headers. This is used for streaming local replies.
   * @param headers The response headers.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void sendResponseHeaders(absl::Span<const HeaderView> headers, bool end_stream) = 0;

  /**
   * Sends response data. This is used for streaming local replies.
   * @param body The response body data.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void sendResponseData(absl::string_view body, bool end_stream) = 0;

  /**
   * Sends response trailers. This is used for streaming local replies.
   * @param trailers The response trailers.
   */
  virtual void sendResponseTrailers(absl::Span<const HeaderView> trailers) = 0;

  /**
   * Adds a custom flag to the current request context.
   * @param flag The custom flag to add.
   */
  virtual void addCustomFlag(absl::string_view flag) = 0;

  /**
   * Continues processing the current request.
   */
  virtual void continueRequest() = 0;

  /**
   * Continues processing the current response.
   */
  virtual void continueResponse() = 0;

  /**
   * Clear route cache to force re-evaluation of route.
   */
  virtual void clearRouteCache() = 0;

  /**
   * Returns reference to request headers.
   * @return Reference to StreamHeaderMap containing request headers.
   */
  virtual HeaderMap& requestHeaders() = 0;

  /**
   * Returns reference to buffered request body.
   * @return Reference to BodyBuffer containing request body.
   */
  virtual BodyBuffer& bufferedRequestBody() = 0;

  /**
   * Returns reference to request trailers.
   * @return Reference to StreamHeaderMap containing request trailers.
   */
  virtual HeaderMap& requestTrailers() = 0;

  /**
   * Returns reference to response headers.
   * @return Reference to StreamHeaderMap containing response headers.
   */
  virtual HeaderMap& responseHeaders() = 0;

  /**
   * Returns reference to buffered response body.
   * @return Reference to BodyBuffer containing response body.
   */
  virtual BodyBuffer& bufferedResponseBody() = 0;

  /**
   * Returns reference to response trailers.
   * @return Reference to StreamHeaderMap containing response trailers.
   */
  virtual HeaderMap& responseTrailers() = 0;

  /**
   * Returns the most specific route configuration for the stream.
   * @return Pointer to RouteSpecificConfig, or nullptr if not available.
   */
  virtual const RouteSpecificConfig* getMostSpecificConfig() = 0;

  /**
   * Returns a scheduler for deferred task execution.
   * @return Unique pointer to Scheduler instance.
   */
  virtual std::shared_ptr<Scheduler> getScheduler() = 0;

  /**
   * Initiates an HTTP callout to a cluster with headers, body, and callback.
   * @param cluster The cluster name.
   * @param headers Span of request headers.
   * @param body The request body.
   * @param timeoutMs The timeout in milliseconds.
   * @param cb Callback invoked when the callout completes. The cb must remain valid
   * for the lifetime of the callout.
   * @return HttpCalloutInitResult and callout ID pair.
   */
  virtual std::pair<HttpCalloutInitResult, uint64_t>
  httpCallout(absl::string_view cluster, absl::Span<const HeaderView> headers,
              absl::string_view body, uint64_t timeout_ms, HttpCalloutCallback& cb) = 0;

  /**
   * Starts a new HTTP stream to an external service.
   * @param cluster The cluster name.
   * @param headers Span of request headers.
   * @param body The initial request body.
   * @param end_of_stream Whether this is the end of the stream.
   * @param timeout_ms The timeout in milliseconds.
   * @param cb Callback invoked for stream events. The cb must remain valid for the lifetime
   * of the stream.
   * @return HttpCalloutInitResult and stream ID pair.
   */
  virtual std::pair<HttpCalloutInitResult, uint64_t>
  startHttpStream(absl::string_view cluster, absl::Span<const HeaderView> headers,
                  absl::string_view body, bool end_of_stream, uint64_t timeout_ms,
                  HttpStreamCallback& cb) = 0;

  /**
   * Sends data on an existing HTTP stream.
   * @param stream_id The ID of the stream.
   * @param body The body data to send.
   * @param end_of_stream Whether this is the end of the stream.
   * @return True if successful, false otherwise.
   */
  virtual bool sendHttpStreamData(uint64_t stream_id, absl::string_view body,
                                  bool end_of_stream) = 0;

  /**
   * Sends trailers on an existing HTTP stream.
   * @param stream_id The ID of the stream.
   * @param trailers The trailers to send.
   * @return True if successful, false otherwise.
   */
  virtual bool sendHttpStreamTrailers(uint64_t stream_id,
                                      absl::Span<const HeaderView> trailers) = 0;

  /**
   * Resets an existing HTTP stream.
   * @param stream_id The ID of the stream.
   */
  virtual void resetHttpStream(uint64_t stream_id) = 0;

  /**
   * Sets the downstream watermark callbacks for the stream.
   * @param callbacks The downstream watermark callbacks.
   */
  virtual void setDownstreamWatermarkCallbacks(DownstreamWatermarkCallbacks& callbacks) = 0;

  /**
   * Unsets the downstream watermark callbacks for the stream.
   */
  virtual void clearDownstreamWatermarkCallbacks() = 0;

  /**
   * Records a histogram value with optional tags.
   * @param id The metric ID.
   * @param value The value to record.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult recordHistogramValue(MetricID id, uint64_t value,
                                             absl::Span<const BufferView> tags_values = {}) = 0;

  /**
   * Sets a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The gauge value.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult setGaugeValue(MetricID id, uint64_t value,
                                      absl::Span<const BufferView> tags_values = {}) = 0;

  /**
   * Increments a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The increment amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult incrementGaugeValue(MetricID id, uint64_t value,
                                            absl::Span<const BufferView> tags_values = {}) = 0;

  /**
   * Decrements a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The decrement amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult decrementGaugeValue(MetricID id, uint64_t value,
                                            absl::Span<const BufferView> tags_values = {}) = 0;

  /**
   * Increments a counter value with optional tags.
   * @param id The metric ID.
   * @param value The increment amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult incrementCounterValue(MetricID id, uint64_t value,
                                              absl::Span<const BufferView> tags_values = {}) = 0;

  /**
   * Checks if logging is enabled for the given log level.
   * @param level The log level to check.
   * @return True if logging is enabled, false otherwise.
   */
  virtual bool logEnabled(LogLevel level) = 0;

  /**
   * Logs a message at the specified log level.
   * @param level The log level.
   * @param message The message to log.
   */
  virtual void log(LogLevel level, absl::string_view message) = 0;
};

class HttpFilterConfigHandle {
public:
  virtual ~HttpFilterConfigHandle();

  /**
   * Defines a histogram metric with a name and optional tag keys.
   * @param name The metric name.
   * @param tags_keys Optional span of tag keys.
   * @return Pair of MetricID and MetricsResult indicating success or failure.
   */
  virtual std::pair<MetricID, MetricsResult>
  defineHistogram(absl::string_view name, absl::Span<const BufferView> tags_keys = {}) = 0;

  /**
   * Defines a gauge metric with a name and optional tag keys.
   * @param name The metric name.
   * @param tags_keys Optional span of tag keys.
   * @return Pair of MetricID and MetricsResult indicating success or failure.
   */
  virtual std::pair<MetricID, MetricsResult>
  defineGauge(absl::string_view name, absl::Span<const BufferView> tags_keys = {}) = 0;

  /**
   * Defines a counter metric with a name and optional tag keys.
   * @param name The metric name.
   * @param tags_keys Optional span of tag keys.
   * @return Pair of MetricID and MetricsResult indicating success or failure.
   */
  virtual std::pair<MetricID, MetricsResult>
  defineCounter(absl::string_view name, absl::Span<const BufferView> tags_keys = {}) = 0;

  /**
   * Checks if logging is enabled for the given log level.
   * @param level The log level to check.
   * @return True if logging is enabled, false otherwise.
   */
  virtual bool logEnabled(LogLevel level) = 0;

  /**
   * Logs a message at the specified log level.
   * @param level The log level.
   * @param message The message to log.
   */
  virtual void log(LogLevel level, absl::string_view message) = 0;
};

/**
 * Interface definitions that plugin developers to implement
 */
enum class HeadersStatus : uint32_t {
  Continue = 0,
  Stop = 1,
  StopAllAndBuffer = 3,
  StopAllAndWatermark = 4,
};

enum class BodyStatus : uint32_t {
  Continue = 0,
  StopAndBuffer = 1,
  StopAndWatermark = 2,
  StopNoBuffer = 3,
};

enum class TrailersStatus : uint32_t {
  Continue = 0,
  Stop = 1,
};

class HttpFilter {
public:
  virtual ~HttpFilter();

  /**
   * Processes request headers. Returns status indicating how to proceed.
   * @param headers The request headers.
   * @param end_stream Indicates if this is the end of the stream.
   * @return HeadersStatus indicating how to proceed.
   */
  virtual HeadersStatus onRequestHeaders(HeaderMap& headers, bool end_stream) = 0;

  /**
   * Processes request body. Returns status indicating how to proceed.
   * @param body The request body buffer.
   * @param end_stream Indicates if this is the end of the stream.
   * @return BodyStatus indicating how to proceed.
   */
  virtual BodyStatus onRequestBody(BodyBuffer& body, bool end_stream) = 0;

  /**
   * Processes request trailers. Returns status indicating how to proceed.
   * @param trailers The request trailers.
   * @return TrailersStatus indicating how to proceed.
   */
  virtual TrailersStatus onRequestTrailers(HeaderMap& trailers) = 0;

  /**
   * Processes response headers. Returns status indicating how to proceed.
   * @param headers The response headers.
   * @param end_stream Indicates if this is the end of the stream.
   * @return HeadersStatus indicating how to proceed.
   */
  virtual HeadersStatus onResponseHeaders(HeaderMap& headers, bool end_stream) = 0;

  /**
   * Processes response body. Returns status indicating how to proceed.
   * @param body The response body buffer.
   * @param end_stream Indicates if this is the end of the stream.
   * @return BodyStatus indicating how to proceed.
   */
  virtual BodyStatus onResponseBody(BodyBuffer& body, bool end_stream) = 0;

  /**
   * Processes response trailers. Returns status indicating how to proceed.
   * @param trailers The response trailers.
   * @return TrailersStatus indicating how to proceed.
   */
  virtual TrailersStatus onResponseTrailers(HeaderMap& trailers) = 0;

  /**
   * Called when the stream processing is complete.
   */
  virtual void onStreamComplete() = 0;
};

class HttpFilterFactory {
public:
  virtual ~HttpFilterFactory();

  /**
   * Creates a StreamPlugin instance for a given stream handle.
   * @param handle The stream plugin handle.
   * @return Unique pointer to a new StreamPlugin instance.
   */
  virtual std::unique_ptr<HttpFilter> create(HttpFilterHandle& handle) = 0;
};

class HttpFilterConfigFactory {
public:
  virtual ~HttpFilterConfigFactory();

  /**
   * Creates a HttpFilterFactory from configuration data.
   * @param handle The stream config handle.
   * @param config_view The unparsed configuration string.
   * @return Unique pointer to a new HttpFilterFactory instance.
   */
  virtual std::unique_ptr<HttpFilterFactory> create(HttpFilterConfigHandle& handle,
                                                    absl::string_view config_view) = 0;

  /**
   * Creates route-specific configuration from unparsed config data.
   * @param handle The stream config handle.
   * @param config_view The unparsed configuration string.
   * @return Unique pointer to a new RouteSpecificConfig instance.
   */
  virtual std::unique_ptr<RouteSpecificConfig>
  createPerRoute([[maybe_unused]] absl::string_view config_view) {
    return nullptr;
  }
};

using HttpFilterConfigFactoryPtr = std::unique_ptr<HttpFilterConfigFactory>;

class HttpFilterConfigFactoryRegistry {
public:
  static const absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>& getRegistry();

private:
  static absl::flat_hash_map<std::string, HttpFilterConfigFactoryPtr>& getMutableRegistry();
  friend class HttpFilterConfigFactoryRegister;
};

class HttpFilterConfigFactoryRegister {
public:
  HttpFilterConfigFactoryRegister(absl::string_view name, HttpFilterConfigFactoryPtr factory);
  ~HttpFilterConfigFactoryRegister();

private:
  const std::string name_;
};

// Macro to register a HttpFilterConfigFactory
#define REGISTER_HTTP_FILTER_CONFIG_FACTORY(FACTORY_CLASS, NAME)                                   \
  static Envoy::DynamicModules::HttpFilterConfigFactoryRegister                                    \
      HttpFilterConfigFactoryRegister_##FACTORY_CLASS##_register_NAME(                             \
          NAME,                                                                                    \
          std::unique_ptr<Envoy::DynamicModules::HttpFilterConfigFactory>(new FACTORY_CLASS()));

// Macro to log messages
#define DYM_LOG(HANDLE, LEVEL, FORMAT_STRING, ...)                                                 \
  do {                                                                                             \
    if (HANDLE.logEnabled(LEVEL)) {                                                                \
      HANDLE.log(LEVEL, std::format(FORMAT_STRING, ##__VA_ARGS__));                                \
    }                                                                                              \
  } while (0)

} // namespace DynamicModules
} // namespace Envoy
