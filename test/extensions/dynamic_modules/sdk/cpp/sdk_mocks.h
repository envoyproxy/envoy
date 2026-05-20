#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "source/extensions/dynamic_modules/sdk/cpp/sdk.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace DynamicModules {

class MockBodyBuffer : public BodyBuffer {
public:
  MOCK_METHOD(std::vector<BufferView>, getChunks, (), (const, override));
  MOCK_METHOD(size_t, getSize, (), (const, override));
  MOCK_METHOD(void, drain, (size_t size), (override));
  MOCK_METHOD(void, append, (std::string_view data), (override));
};

class MockHeaderMap : public HeaderMap {
public:
  MOCK_METHOD(std::vector<std::string_view>, get, (std::string_view key), (const, override));
  MOCK_METHOD(std::string_view, getOne, (std::string_view key), (const, override));
  MOCK_METHOD(std::vector<HeaderView>, getAll, (), (const, override));
  MOCK_METHOD(size_t, size, (), (const, override));
  MOCK_METHOD(void, set, (std::string_view key, std::string_view value), (override));
  MOCK_METHOD(void, add, (std::string_view key, std::string_view value), (override));
  MOCK_METHOD(void, remove, (std::string_view key), (override));
};

class MockScheduler : public Scheduler {
public:
  MOCK_METHOD(void, schedule, (std::function<void()> func), (override));
};

class MockHttpCalloutCallback : public HttpCalloutCallback {
public:
  MOCK_METHOD(void, onHttpCalloutDone,
              (HttpCalloutResult result, std::span<const HeaderView> headers,
               std::span<const BufferView> body_chunks),
              (override));
};

class MockHttpStreamCallback : public HttpStreamCallback {
public:
  MOCK_METHOD(void, onHttpStreamHeaders,
              (uint64_t stream_id, std::span<const HeaderView> headers, bool end_stream),
              (override));
  MOCK_METHOD(void, onHttpStreamData,
              (uint64_t stream_id, std::span<const BufferView> body, bool end_stream), (override));
  MOCK_METHOD(void, onHttpStreamTrailers,
              (uint64_t stream_id, std::span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, onHttpStreamComplete, (uint64_t stream_id), (override));
  MOCK_METHOD(void, onHttpStreamReset, (uint64_t stream_id, HttpStreamResetReason reason),
              (override));
};

class MockDownstreamWatermarkCallbacks : public DownstreamWatermarkCallbacks {
public:
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
};

class MockHttpFilterConfigHandle : public HttpFilterConfigHandle {
public:
  MOCK_METHOD((std::pair<MetricID, MetricsResult>), defineHistogram,
              (std::string_view name, std::span<const BufferView> tags_keys), (override));
  MOCK_METHOD((std::pair<MetricID, MetricsResult>), defineGauge,
              (std::string_view name, std::span<const BufferView> tags_keys), (override));
  MOCK_METHOD((std::pair<MetricID, MetricsResult>), defineCounter,
              (std::string_view name, std::span<const BufferView> tags_keys), (override));
  MOCK_METHOD(bool, logEnabled, (LogLevel level), (override));
  MOCK_METHOD(void, log, (LogLevel level, std::string_view message), (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), httpCallout,
              (std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
               uint64_t timeout_ms, HttpCalloutCallback& cb),
              (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), startHttpStream,
              (std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
               bool end_of_stream, uint64_t timeout_ms, HttpStreamCallback& cb),
              (override));
  MOCK_METHOD(bool, sendHttpStreamData,
              (uint64_t stream_id, std::string_view body, bool end_of_stream), (override));
  MOCK_METHOD(bool, sendHttpStreamTrailers,
              (uint64_t stream_id, std::span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, resetHttpStream, (uint64_t stream_id), (override));
  MOCK_METHOD(std::shared_ptr<Scheduler>, getScheduler, (), (override));
};

class MockHttpFilterHandle : public HttpFilterHandle {
public:
  MOCK_METHOD(std::optional<std::string_view>, getMetadataString,
              (std::string_view ns, std::string_view key), (override));
  MOCK_METHOD(std::optional<double>, getMetadataNumber, (std::string_view ns, std::string_view key),
              (override));
  MOCK_METHOD(std::optional<bool>, getMetadataBool, (std::string_view ns, std::string_view key),
              (override));
  MOCK_METHOD(std::vector<std::string_view>, getMetadataKeys, (std::string_view ns), (override));
  MOCK_METHOD(std::vector<std::string_view>, getMetadataNamespaces, (), (override));
  MOCK_METHOD(void, setMetadata,
              (std::string_view ns, std::string_view key, std::string_view value), (override));
  MOCK_METHOD(void, setMetadata, (std::string_view ns, std::string_view key, double value),
              (override));
  MOCK_METHOD(void, setMetadata, (std::string_view ns, std::string_view key, bool value),
              (override));
  MOCK_METHOD(bool, addMetadataList, (std::string_view ns, std::string_view key, double value),
              (override));
  MOCK_METHOD(bool, addMetadataList,
              (std::string_view ns, std::string_view key, std::string_view value), (override));
  MOCK_METHOD(bool, addMetadataList, (std::string_view ns, std::string_view key, bool value),
              (override));
  MOCK_METHOD(std::optional<size_t>, getMetadataListSize,
              (std::string_view ns, std::string_view key), (override));
  MOCK_METHOD(std::optional<double>, getMetadataListNumber,
              (std::string_view ns, std::string_view key, size_t index), (override));
  MOCK_METHOD(std::optional<std::string_view>, getMetadataListString,
              (std::string_view ns, std::string_view key, size_t index), (override));
  MOCK_METHOD(std::optional<bool>, getMetadataListBool,
              (std::string_view ns, std::string_view key, size_t index), (override));
  MOCK_METHOD(std::optional<std::string_view>, getAttributeString, (AttributeID id), (override));
  MOCK_METHOD(std::optional<uint64_t>, getAttributeNumber, (AttributeID id), (override));
  MOCK_METHOD(std::optional<bool>, getAttributeBool, (AttributeID id), (override));
  MOCK_METHOD(std::optional<std::string_view>, getFilterState, (std::string_view key), (override));
  MOCK_METHOD(void, setFilterState, (std::string_view key, std::string_view value), (override));
  MOCK_METHOD(void, sendLocalResponse,
              (uint32_t status, std::span<const HeaderView> headers, std::string_view body,
               std::string_view detail),
              (override));
  MOCK_METHOD(void, sendResponseHeaders, (std::span<const HeaderView> headers, bool end_stream),
              (override));
  MOCK_METHOD(void, sendResponseData, (std::string_view body, bool end_stream), (override));
  MOCK_METHOD(void, sendResponseTrailers, (std::span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, addCustomFlag, (std::string_view flag), (override));
  MOCK_METHOD(void, continueRequest, (), (override));
  MOCK_METHOD(void, continueResponse, (), (override));
  MOCK_METHOD(void, clearRouteCache, (), (override));
  MOCK_METHOD(void, refreshRouteCluster, (), (override));
  MOCK_METHOD(uint64_t, getBufferLimit, (), (override));
  MOCK_METHOD(void, setBufferLimit, (uint64_t limit), (override));
  MOCK_METHOD(std::optional<std::string_view>, getFilterStateTyped, (std::string_view key),
              (override));
  MOCK_METHOD(bool, setFilterStateTyped, (std::string_view key, std::string_view value),
              (override));
  MOCK_METHOD(uint32_t, getWorkerIndex, (), (override));
  MOCK_METHOD(bool, setSocketOptionInt,
              (int64_t level, int64_t name, SocketOptionState state, SocketDirection direction,
               int64_t value),
              (override));
  MOCK_METHOD(bool, setSocketOptionBytes,
              (int64_t level, int64_t name, SocketOptionState state, SocketDirection direction,
               std::string_view value),
              (override));
  MOCK_METHOD(std::optional<int64_t>, getSocketOptionInt,
              (int64_t level, int64_t name, SocketOptionState state, SocketDirection direction),
              (override));
  MOCK_METHOD(std::optional<std::string_view>, getSocketOptionBytes,
              (int64_t level, int64_t name, SocketOptionState state, SocketDirection direction),
              (override));
  MOCK_METHOD(std::unique_ptr<Span>, getActiveSpan, (), (override));
  MOCK_METHOD(std::optional<std::string_view>, getClusterName, (), (override));
  MOCK_METHOD(std::optional<ClusterHostCounts>, getClusterHostCounts, (uint32_t priority),
              (override));
  MOCK_METHOD(bool, setUpstreamOverrideHost, (std::string_view host, bool strict), (override));
  MOCK_METHOD(void, resetStream, (HttpFilterStreamResetReason reason, std::string_view details),
              (override));
  MOCK_METHOD(void, sendGoAwayAndClose, (bool graceful), (override));
  MOCK_METHOD(bool, recreateStream, (std::span<const HeaderView> headers), (override));
  MOCK_METHOD(HeaderMap&, requestHeaders, (), (override));
  MOCK_METHOD(BodyBuffer&, bufferedRequestBody, (), (override));
  MOCK_METHOD(BodyBuffer&, receivedRequestBody, (), (override));
  MOCK_METHOD(bool, receivedBufferedRequestBody, (), (override));
  MOCK_METHOD(HeaderMap&, requestTrailers, (), (override));
  MOCK_METHOD(HeaderMap&, responseHeaders, (), (override));
  MOCK_METHOD(BodyBuffer&, bufferedResponseBody, (), (override));
  MOCK_METHOD(BodyBuffer&, receivedResponseBody, (), (override));
  MOCK_METHOD(bool, receivedBufferedResponseBody, (), (override));
  MOCK_METHOD(HeaderMap&, responseTrailers, (), (override));
  MOCK_METHOD(const RouteSpecificConfig*, getMostSpecificConfig, (), (override));
  MOCK_METHOD(std::shared_ptr<Scheduler>, getScheduler, (), (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), httpCallout,
              (std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
               uint64_t timeout_ms, HttpCalloutCallback& cb),
              (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), startHttpStream,
              (std::string_view cluster, std::span<const HeaderView> headers, std::string_view body,
               bool end_of_stream, uint64_t timeout_ms, HttpStreamCallback& cb),
              (override));
  MOCK_METHOD(bool, sendHttpStreamData,
              (uint64_t stream_id, std::string_view body, bool end_of_stream), (override));
  MOCK_METHOD(bool, sendHttpStreamTrailers,
              (uint64_t stream_id, std::span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, resetHttpStream, (uint64_t stream_id), (override));
  MOCK_METHOD(void, setDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, clearDownstreamWatermarkCallbacks, (), (override));
  MOCK_METHOD(MetricsResult, recordHistogramValue,
              (MetricID id, uint64_t value, std::span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, setGaugeValue,
              (MetricID id, uint64_t value, std::span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, incrementGaugeValue,
              (MetricID id, uint64_t value, std::span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, decrementGaugeValue,
              (MetricID id, uint64_t value, std::span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, incrementCounterValue,
              (MetricID id, uint64_t value, std::span<const BufferView> tags_values), (override));
  MOCK_METHOD(bool, logEnabled, (LogLevel level), (override));
  MOCK_METHOD(void, log, (LogLevel level, std::string_view message), (override));
};

class MockHttpFilter : public HttpFilter {
public:
  MOCK_METHOD(HeadersStatus, onRequestHeaders, (HeaderMap & headers, bool end_stream), (override));
  MOCK_METHOD(BodyStatus, onRequestBody, (BodyBuffer & body, bool end_stream), (override));
  MOCK_METHOD(TrailersStatus, onRequestTrailers, (HeaderMap & trailers), (override));
  MOCK_METHOD(HeadersStatus, onResponseHeaders, (HeaderMap & headers, bool end_stream), (override));
  MOCK_METHOD(BodyStatus, onResponseBody, (BodyBuffer & body, bool end_stream), (override));
  MOCK_METHOD(TrailersStatus, onResponseTrailers, (HeaderMap & trailers), (override));
  MOCK_METHOD(void, onStreamComplete, (), (override));
  MOCK_METHOD(void, onDestroy, (), (override));
};

} // namespace DynamicModules
} // namespace Envoy
