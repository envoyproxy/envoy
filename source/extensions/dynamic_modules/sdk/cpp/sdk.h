#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
  BufferView(std::string_view data) : data_(data.data()), length_(data.size()) {}
  BufferView(const char* data, size_t length) : data_(data), length_(length) {}
  /**
   * Default constructor.
   */
  BufferView() = default;

  /**
   * Returns the buffer as a string_view.
   * @return The buffer contents as a string_view.
   */
  std::string_view toStringView() const { return {data_, length_}; }

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
  HeaderView(std::string_view key, std::string_view value)
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
  std::string_view key() const { return {key_data_, key_length_}; }

  /**
   * Returns the header value as a string_view.
   * @return The header value.
   */
  std::string_view value() const { return {value_data_, value_length_}; }

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
  virtual std::vector<BufferView> getChunks() const = 0;

  /**
   * Returns the total size of the buffer.
   * @return The total size in bytes.
   */
  virtual size_t getSize() const = 0;

  /**
   * Removes size from the front of the buffer.
   * @param size Number of bytes to remove.
   */
  virtual void drain(size_t size) = 0;

  /**
   * Appends data to the buffer.
   * @param data The data to append.
   */
  virtual void append(std::string_view data) = 0;
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
  virtual std::vector<std::string_view> get(std::string_view key) const = 0;

  /**
   * Returns the first value for a given header key.
   * @param key The header key to look up.
   * @return The first header value, or empty if not found.
   */
  virtual std::string_view getOne(std::string_view key) const = 0;

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
  virtual void set(std::string_view key, std::string_view value) = 0;

  /**
   * Adds a header key-value pair (appends to existing).
   * @param key The header key.
   * @param value The header value.
   */
  virtual void add(std::string_view key, std::string_view value) = 0;

  /**
   * Removes all headers with the given key.
   * @param key The header key to remove.
   */
  virtual void remove(std::string_view key) = 0;
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
  XdsFilterChainName,
  HealthCheck
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
  virtual void onHttpCalloutDone(HttpCalloutResult result, std::span<const HeaderView> headers,
                                 std::span<const BufferView> body_chunks) = 0;
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
  virtual void onHttpStreamHeaders(uint64_t stream_id, std::span<const HeaderView> headers,
                                   bool end_stream) = 0;

  /**
   * Called when response data is received.
   * @param stream_id The ID of the stream.
   * @param body The response body chunks.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void onHttpStreamData(uint64_t stream_id, std::span<const BufferView> body,
                                bool end_stream) = 0;

  /**
   * Called when response trailers are received.
   * @param stream_id The ID of the stream.
   * @param trailers The response trailers.
   */
  virtual void onHttpStreamTrailers(uint64_t stream_id, std::span<const HeaderView> trailers) = 0;

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

enum class HttpFilterStreamResetReason : uint32_t {
  LocalReset,
  LocalRefusedStreamReset,
};

enum class SocketOptionState : uint32_t {
  Prebind,
  Bound,
  Listening,
};

enum class SocketDirection : uint32_t { Upstream, Downstream };

struct ClusterHostCounts {
  uint64_t total;
  uint64_t healthy;
  uint64_t degraded;
};

class ChildSpan;

class Span {
public:
  virtual ~Span() = default;

  virtual void setTag(std::string_view key, std::string_view value) = 0;
  virtual void setOperation(std::string_view operation) = 0;
  virtual void log(std::string_view event) = 0;
  virtual void setSampled(bool sampled) = 0;
  virtual std::optional<std::string_view> getBaggage(std::string_view key) = 0;
  virtual void setBaggage(std::string_view key, std::string_view value) = 0;
  virtual std::optional<std::string_view> getTraceID() = 0;
  virtual std::optional<std::string_view> getSpanID() = 0;
  virtual std::unique_ptr<ChildSpan> spawnChild(std::string_view operation) = 0;
};

class ChildSpan : public Span {
public:
  virtual void finish() = 0;
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
  virtual std::optional<std::string_view> getMetadataString(std::string_view ns,
                                                            std::string_view key) = 0;

  /**
   * Retrieves a numeric metadata value by namespace and key.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @return Pair of double and bool indicating if value was found.
   */
  virtual std::optional<double> getMetadataNumber(std::string_view ns, std::string_view key) = 0;

  /**
   * Retrieves a bool metadata value by namespace and key.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @return The bool value if found, otherwise nullopt.
   */
  virtual std::optional<bool> getMetadataBool(std::string_view ns, std::string_view key) = 0;

  /**
   * Retrieves all keys in a metadata namespace.
   * @param ns The metadata namespace.
   * @return Vector of key strings.
   */
  virtual std::vector<std::string_view> getMetadataKeys(std::string_view ns) = 0;

  /**
   * Retrieves all namespace names in the metadata.
   * @return Vector of namespace name strings.
   */
  virtual std::vector<std::string_view> getMetadataNamespaces() = 0;

  /**
   * Sets a string metadata value.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The string value to set.
   */
  virtual void setMetadata(std::string_view ns, std::string_view key, std::string_view value) = 0;

  /**
   * Sets a numeric metadata value.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The numeric value to set.
   */
  virtual void setMetadata(std::string_view ns, std::string_view key, double value) = 0;

  /**
   * Sets a bool metadata value.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The bool value to set.
   */
  virtual void setMetadata(std::string_view ns, std::string_view key, bool value) = 0;

  // Prevent const char* from implicitly converting to bool instead of string_view.
  void setMetadata(std::string_view ns, std::string_view key, const char* value) {
    setMetadata(ns, key, std::string_view(value));
  }

  /**
   * Appends a numeric value to the dynamic metadata list stored under the given namespace and key.
   * If the key does not exist, a new list is created. Returns false if the key exists but is not a
   * list, or if the metadata is not accessible.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The number to append.
   * @return true if the operation is successful, false otherwise.
   */
  virtual bool addMetadataList(std::string_view ns, std::string_view key, double value) = 0;

  /**
   * Appends a string value to the dynamic metadata list stored under the given namespace and key.
   * If the key does not exist, a new list is created. Returns false if the key exists but is not a
   * list, or if the metadata is not accessible.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The string to append.
   * @return true if the operation is successful, false otherwise.
   */
  virtual bool addMetadataList(std::string_view ns, std::string_view key,
                               std::string_view value) = 0;

  // Prevent const char* from implicitly converting to bool instead of string_view.
  bool addMetadataList(std::string_view ns, std::string_view key, const char* value) {
    return addMetadataList(ns, key, std::string_view(value));
  }

  /**
   * Appends a bool value to the dynamic metadata list stored under the given namespace and key.
   * If the key does not exist, a new list is created. Returns false if the key exists but is not a
   * list, or if the metadata is not accessible.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param value The bool to append.
   * @return true if the operation is successful, false otherwise.
   */
  virtual bool addMetadataList(std::string_view ns, std::string_view key, bool value) = 0;

  /**
   * Returns the number of elements in the metadata list stored under the given namespace and key.
   * Returns nullopt if the metadata is not accessible, the namespace or key does not exist, or the
   * value is not a list.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   */
  virtual std::optional<size_t> getMetadataListSize(std::string_view ns, std::string_view key) = 0;

  /**
   * Returns the number element at the given index in the metadata list stored under the given
   * namespace and key. Returns nullopt if the metadata is not accessible, the namespace or key does
   * not exist, the value is not a list, the index is out of range, or the element is not a number.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param index The zero-based index.
   */
  virtual std::optional<double> getMetadataListNumber(std::string_view ns, std::string_view key,
                                                      size_t index) = 0;

  /**
   * Returns the string element at the given index in the metadata list stored under the given
   * namespace and key. Returns nullopt if the metadata is not accessible, the namespace or key does
   * not exist, the value is not a list, the index is out of range, or the element is not a string.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param index The zero-based index.
   */
  virtual std::optional<std::string_view>
  getMetadataListString(std::string_view ns, std::string_view key, size_t index) = 0;

  /**
   * Returns the bool element at the given index in the metadata list stored under the given
   * namespace and key. Returns nullopt if the metadata is not accessible, the namespace or key does
   * not exist, the value is not a list, the index is out of range, or the element is not a bool.
   * @param ns The metadata namespace.
   * @param key The metadata key.
   * @param index The zero-based index.
   */
  virtual std::optional<bool> getMetadataListBool(std::string_view ns, std::string_view key,
                                                  size_t index) = 0;

  /**
   * Retrieves the serialized filter state value of the stream.
   * @param key The filter state key.
   * @return The filter state value if found, otherwise empty.
   */
  virtual std::optional<std::string_view> getFilterState(std::string_view key) = 0;

  /**
   * Sets the serialized filter state value of the stream.
   * @param key The filter state key.
   * @param value The filter state value.
   */
  virtual void setFilterState(std::string_view key, std::string_view value) = 0;

  /**
   * Retrieves a string attribute value.
   * @param id The attribute ID.
   * @return Pair of string view and bool indicating if attribute was found.
   */
  virtual std::optional<std::string_view> getAttributeString(AttributeID id) = 0;

  /**
   * Retrieves a numeric attribute value.
   * @param id The attribute ID.
   * @return Pair of double and bool indicating if attribute was found.
   */
  virtual std::optional<uint64_t> getAttributeNumber(AttributeID id) = 0;

  /**
   * Retrieves a boolean attribute value.
   * @param id The attribute ID.
   * @return The bool value if found, otherwise nullopt.
   */
  virtual std::optional<bool> getAttributeBool(AttributeID id) = 0;

  /**
   * Sends a local response with status code, body, and detail.
   * @param status The HTTP status code.
   * @param body The response body.
   * @param detail The response detail.
   */
  virtual void sendLocalResponse(uint32_t status, std::span<const HeaderView> headers,
                                 std::string_view body, std::string_view detail) = 0;

  /**
   * Sends response headers. This is used for streaming local replies.
   * @param headers The response headers.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void sendResponseHeaders(std::span<const HeaderView> headers, bool end_stream) = 0;

  /**
   * Sends response data. This is used for streaming local replies.
   * @param body The response body data.
   * @param end_stream Indicates if this is the end of the stream.
   */
  virtual void sendResponseData(std::string_view body, bool end_stream) = 0;

  /**
   * Sends response trailers. This is used for streaming local replies.
   * @param trailers The response trailers.
   */
  virtual void sendResponseTrailers(std::span<const HeaderView> trailers) = 0;

  /**
   * Adds a custom flag to the current request context.
   * @param flag The custom flag to add.
   */
  virtual void addCustomFlag(std::string_view flag) = 0;

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
   * Clear only the cluster selection for the current route without clearing the entire route cache.
   *
   * This is a subset of clearRouteCache(). Use this when a filter modifies headers that affect
   * cluster selection but not the route itself. This is more efficient than clearing the entire
   * route cache.
   */
  virtual void refreshRouteCluster() = 0;

  /**
   * Returns the current body buffering limit in bytes.
   * @return The buffer limit in bytes.
   */
  virtual uint64_t getBufferLimit() = 0;

  /**
   * Sets the current body buffering limit in bytes.
   * @param limit The desired buffer limit in bytes.
   */
  virtual void setBufferLimit(uint64_t limit) = 0;

  /**
   * Retrieves the serialized typed filter state value of the stream.
   * @param key The filter state key.
   * @return The typed filter state value if found, otherwise empty.
   */
  virtual std::optional<std::string_view> getFilterStateTyped(std::string_view key) = 0;

  /**
   * Sets a typed filter state value of the stream.
   * @param key The filter state key.
   * @param value The serialized typed value.
   * @return true if the value was stored successfully.
   */
  virtual bool setFilterStateTyped(std::string_view key, std::string_view value) = 0;

  /**
   * Returns the worker index assigned to the current filter instance.
   */
  virtual uint32_t getWorkerIndex() = 0;

  /**
   * Sets an integer socket option on the upstream or downstream connection.
   */
  virtual bool setSocketOptionInt(int64_t level, int64_t name, SocketOptionState state,
                                  SocketDirection direction, int64_t value) = 0;

  /**
   * Sets a bytes socket option on the upstream or downstream connection.
   */
  virtual bool setSocketOptionBytes(int64_t level, int64_t name, SocketOptionState state,
                                    SocketDirection direction, std::string_view value) = 0;

  /**
   * Retrieves an integer socket option from the upstream or downstream connection.
   */
  virtual std::optional<int64_t> getSocketOptionInt(int64_t level, int64_t name,
                                                    SocketOptionState state,
                                                    SocketDirection direction) = 0;

  /**
   * Retrieves a bytes socket option from the upstream or downstream connection.
   */
  virtual std::optional<std::string_view> getSocketOptionBytes(int64_t level, int64_t name,
                                                               SocketOptionState state,
                                                               SocketDirection direction) = 0;

  /**
   * Retrieves the active tracing span for the current stream.
   */
  virtual std::unique_ptr<Span> getActiveSpan() = 0;

  /**
   * Retrieves the selected upstream cluster name for the current stream.
   */
  virtual std::optional<std::string_view> getClusterName() = 0;

  /**
   * Retrieves host counts for the selected upstream cluster at the given priority.
   */
  virtual std::optional<ClusterHostCounts> getClusterHostCounts(uint32_t priority) = 0;

  /**
   * Sets an upstream override host for the selected cluster.
   */
  virtual bool setUpstreamOverrideHost(std::string_view host, bool strict) = 0;

  /**
   * Resets the current downstream stream with the given reason and details string.
   */
  virtual void resetStream(HttpFilterStreamResetReason reason, std::string_view details) = 0;

  /**
   * Sends GOAWAY and closes the downstream connection.
   */
  virtual void sendGoAwayAndClose(bool graceful) = 0;

  /**
   * Recreates the current stream, optionally with replacement headers.
   */
  virtual bool recreateStream(std::span<const HeaderView> headers = {}) = 0;

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
   * Returns reference to the latest received request body chunk.
   * NOTE: This is only valid in the onRequestBody callback, and it retrieves the latest received
   * body chunk that triggers the callback. For other callbacks or outside of the callbacks, you
   * should use bufferedRequestBody() to get the currently buffered body in the chain.
   * @return Reference to BodyBuffer containing the latest received request body chunk.
   */
  virtual BodyBuffer& receivedRequestBody() = 0;

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
   * Returns reference to the latest received response body chunk.
   * NOTE: This is only valid in the onResponseBody callback, and it retrieves the latest received
   * body chunk that triggers the callback. For other callbacks or outside of the callbacks, you
   * should use bufferedResponseBody() to get the currently buffered body in the chain.
   * @return Reference to BodyBuffer containing the latest received response body chunk.
   */
  virtual BodyBuffer& receivedResponseBody() = 0;

  /**
   * Returns true if the latest received request body is the previously buffered request body.
   * This is true when a previous filter in the chain stopped and buffered the request body, then
   * resumed, and this filter is now receiving that buffered body.
   * NOTE: This is only meaningful inside the onRequestBody callback.
   * @return true if the received request body is the previously buffered request body.
   */
  virtual bool receivedBufferedRequestBody() = 0;

  /**
   * Returns true if the latest received response body is the previously buffered response body.
   * This is true when a previous filter in the chain stopped and buffered the response body, then
   * resumed, and this filter is now receiving that buffered body.
   * NOTE: This is only meaningful inside the onResponseBody callback.
   * @return true if the received response body is the previously buffered response body.
   */
  virtual bool receivedBufferedResponseBody() = 0;

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
  httpCallout(std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
              uint64_t timeout_ms, HttpCalloutCallback& cb) = 0;

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
  startHttpStream(std::string_view cluster, std::span<const HeaderView> headers,
                  std::string_view body, bool end_of_stream, uint64_t timeout_ms,
                  HttpStreamCallback& cb) = 0;

  /**
   * Sends data on an existing HTTP stream.
   * @param stream_id The ID of the stream.
   * @param body The body data to send.
   * @param end_of_stream Whether this is the end of the stream.
   * @return True if successful, false otherwise.
   */
  virtual bool sendHttpStreamData(uint64_t stream_id, std::string_view body,
                                  bool end_of_stream) = 0;

  /**
   * Sends trailers on an existing HTTP stream.
   * @param stream_id The ID of the stream.
   * @param trailers The trailers to send.
   * @return True if successful, false otherwise.
   */
  virtual bool sendHttpStreamTrailers(uint64_t stream_id, std::span<const HeaderView> trailers) = 0;

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
                                             std::span<const BufferView> tags_values = {}) = 0;

  /**
   * Sets a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The gauge value.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult setGaugeValue(MetricID id, uint64_t value,
                                      std::span<const BufferView> tags_values = {}) = 0;

  /**
   * Increments a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The increment amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult incrementGaugeValue(MetricID id, uint64_t value,
                                            std::span<const BufferView> tags_values = {}) = 0;

  /**
   * Decrements a gauge value with optional tags.
   * @param id The metric ID.
   * @param value The decrement amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult decrementGaugeValue(MetricID id, uint64_t value,
                                            std::span<const BufferView> tags_values = {}) = 0;

  /**
   * Increments a counter value with optional tags.
   * @param id The metric ID.
   * @param value The increment amount.
   * @param tags_values Optional span of tag values.
   * @return MetricsResult indicating success or failure.
   */
  virtual MetricsResult incrementCounterValue(MetricID id, uint64_t value,
                                              std::span<const BufferView> tags_values = {}) = 0;

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
  virtual void log(LogLevel level, std::string_view message) = 0;
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
  defineHistogram(std::string_view name, std::span<const BufferView> tags_keys = {}) = 0;

  /**
   * Defines a gauge metric with a name and optional tag keys.
   * @param name The metric name.
   * @param tags_keys Optional span of tag keys.
   * @return Pair of MetricID and MetricsResult indicating success or failure.
   */
  virtual std::pair<MetricID, MetricsResult>
  defineGauge(std::string_view name, std::span<const BufferView> tags_keys = {}) = 0;

  /**
   * Defines a counter metric with a name and optional tag keys.
   * @param name The metric name.
   * @param tags_keys Optional span of tag keys.
   * @return Pair of MetricID and MetricsResult indicating success or failure.
   */
  virtual std::pair<MetricID, MetricsResult>
  defineCounter(std::string_view name, std::span<const BufferView> tags_keys = {}) = 0;

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
  virtual void log(LogLevel level, std::string_view message) = 0;

  /**
   * Initiates a one-shot HTTP callout to a cluster. The response will be delivered via
   * HttpFilterConfigFactory::onHttpCalloutDone.
   * @param cluster The cluster name.
   * @param headers The request headers. Must include :method, :path, and host headers.
   * @param body The request body.
   * @param timeout_ms The timeout in milliseconds.
   * @param cb The callback to invoke when the callout completes.
   * @return HttpCalloutInitResult and callout ID pair.
   */
  virtual std::pair<HttpCalloutInitResult, uint64_t>
  httpCallout(std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
              uint64_t timeout_ms, HttpCalloutCallback& cb) = 0;

  /**
   * Starts a streamable HTTP callout to a cluster. Stream events will be delivered via
   * HttpFilterConfigFactory::onHttpStream* methods.
   * @param cluster The cluster name.
   * @param headers The request headers. Must include :method, :path, and host headers.
   * @param body The initial request body (may be empty).
   * @param end_of_stream If true, the stream ends after sending the initial headers/body.
   * @param timeout_ms The timeout in milliseconds (0 for no timeout).
   * @param cb The callback to invoke for stream events.
   * @return HttpCalloutInitResult and stream ID pair.
   */
  virtual std::pair<HttpCalloutInitResult, uint64_t>
  startHttpStream(std::string_view cluster, std::span<const HeaderView> headers,
                  std::string_view body, bool end_of_stream, uint64_t timeout_ms,
                  HttpStreamCallback& cb) = 0;

  /**
   * Sends data on an active stream started via startHttpStream.
   * @param stream_id The stream handle returned from startHttpStream.
   * @param body The data to send.
   * @param end_of_stream If true, this is the last data chunk.
   * @return True if successful, false if the stream is not found.
   */
  virtual bool sendHttpStreamData(uint64_t stream_id, std::string_view body,
                                  bool end_of_stream) = 0;

  /**
   * Sends trailers on an active stream, implicitly ending the stream.
   * @param stream_id The stream handle returned from startHttpStream.
   * @param trailers The trailers to send.
   * @return True if successful, false if the stream is not found.
   */
  virtual bool sendHttpStreamTrailers(uint64_t stream_id, std::span<const HeaderView> trailers) = 0;

  /**
   * Resets an active stream started via startHttpStream.
   * @param stream_id The stream handle returned from startHttpStream.
   */
  virtual void resetHttpStream(uint64_t stream_id) = 0;

  /**
   * Returns a scheduler for deferred task execution. This can only be called on config loading
   * event and then the returned Scheduler can be used in other threads.
   * @return Unique pointer to Scheduler instance.
   */
  virtual std::shared_ptr<Scheduler> getScheduler() = 0;
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

enum class LocalReplyStatus : uint32_t {
  Continue = 0,
  ContinueAndResetStream = 1,
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
   * Called when the stream processing is complete and before access logs are flushed.
   * This is a good place to do any final processing or cleanup before the request is fully
   * completed.
   */
  virtual void onStreamComplete() = 0;

  /**
   * Called when the HTTP filter instance is being destroyed. This is called
   * after onStreamComplete and access logs are flushed. This is a good place to release
   * any per-stream resources.
   */
  virtual void onDestroy() = 0;

  /**
   * Called when a local reply is being sent on the stream.
   * @param response_code The HTTP response code for the local reply.
   * @param details The response code details string.
   * @param reset_imminent Whether the stream will be reset instead of sending the local reply.
   * @return LocalReplyStatus indicating how local reply processing should continue.
   */
  virtual LocalReplyStatus onLocalReply(uint32_t response_code, std::string_view details,
                                        bool reset_imminent) {
    return LocalReplyStatus::Continue;
  }
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
                                                    std::string_view config_view) = 0;

  /**
   * Creates route-specific configuration from unparsed config data.
   * @param handle The stream config handle.
   * @param config_view The unparsed configuration string.
   * @return Unique pointer to a new RouteSpecificConfig instance.
   */
  virtual std::unique_ptr<RouteSpecificConfig>
  createPerRoute([[maybe_unused]] std::string_view config_view) {
    return nullptr;
  }
};

using HttpFilterConfigFactoryPtr = std::unique_ptr<HttpFilterConfigFactory>;

namespace Utility {

/**
 * Reads the whole request body by combining the buffered body and the latest received body.
 * This will copy all request body content into a module owned string.
 *
 * This should only be called after we see the end of the request, which means the end_of_stream
 * flag is true in the onRequestBody callback or we are in the onRequestTrailers callback.
 * @param handle The HTTP filter handle.
 * @return The combined request body as a string.
 */
std::string readWholeRequestBody(HttpFilterHandle& handle);

/**
 * Reads the whole response body by combining the buffered body and the latest received body.
 * This will copy all response body content into a module owned string.
 *
 * This should only be called after we see the end of the response, which means the end_of_stream
 * flag is true in the onResponseBody callback or we are in the onResponseTrailers callback.
 * @param handle The HTTP filter handle.
 * @return The combined response body as a string.
 */
std::string readWholeResponseBody(HttpFilterHandle& handle);

} // namespace Utility

class HttpFilterConfigFactoryRegistry {
public:
  static const std::map<std::string_view, HttpFilterConfigFactoryPtr>& getRegistry();

private:
  static std::map<std::string_view, HttpFilterConfigFactoryPtr>& getMutableRegistry();
  friend class HttpFilterConfigFactoryRegister;
};

class HttpFilterConfigFactoryRegister {
public:
  HttpFilterConfigFactoryRegister(std::string_view name, HttpFilterConfigFactoryPtr factory);
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
