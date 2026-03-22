#pragma once

#include "gmock/gmock.h"
#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

class MockBodyBuffer : public BodyBuffer {
public:
  MOCK_METHOD(std::vector<BufferView>, getChunks, (), (override));
  MOCK_METHOD(size_t, getSize, (), (override));
  MOCK_METHOD(void, drain, (size_t size), (override));
  MOCK_METHOD(void, append, (absl::string_view data), (override));
};

class MockHeaderMap : public HeaderMap {
public:
  MOCK_METHOD(std::vector<absl::string_view>, get, (absl::string_view key), (const, override));
  MOCK_METHOD(absl::string_view, getOne, (absl::string_view key), (const, override));
  MOCK_METHOD(std::vector<HeaderView>, getAll, (), (const, override));
  MOCK_METHOD(size_t, size, (), (const, override));
  MOCK_METHOD(void, set, (absl::string_view key, absl::string_view value), (override));
  MOCK_METHOD(void, add, (absl::string_view key, absl::string_view value), (override));
  MOCK_METHOD(void, remove, (absl::string_view key), (override));
};

class MockScheduler : public Scheduler {
public:
  MOCK_METHOD(void, schedule, (std::function<void()> func), (override));
};

class MockHttpCalloutCallback : public HttpCalloutCallback {
public:
  MOCK_METHOD(void, onHttpCalloutDone,
              (HttpCalloutResult result, absl::Span<const HeaderView> headers,
               absl::Span<const BufferView> body_chunks),
              (override));
};

class MockHttpStreamCallback : public HttpStreamCallback {
public:
  MOCK_METHOD(void, onHttpStreamHeaders,
              (uint64_t stream_id, absl::Span<const HeaderView> headers, bool end_stream),
              (override));
  MOCK_METHOD(void, onHttpStreamData,
              (uint64_t stream_id, absl::Span<const BufferView> body, bool end_stream), (override));
  MOCK_METHOD(void, onHttpStreamTrailers,
              (uint64_t stream_id, absl::Span<const HeaderView> trailers), (override));
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
              (absl::string_view name, absl::Span<const BufferView> tags_keys), (override));
  MOCK_METHOD((std::pair<MetricID, MetricsResult>), defineGauge,
              (absl::string_view name, absl::Span<const BufferView> tags_keys), (override));
  MOCK_METHOD((std::pair<MetricID, MetricsResult>), defineCounter,
              (absl::string_view name, absl::Span<const BufferView> tags_keys), (override));
  MOCK_METHOD(bool, logEnabled, (LogLevel level), (override));
  MOCK_METHOD(void, log, (LogLevel level, absl::string_view message), (override));
};

class MockHttpFilterHandle : public HttpFilterHandle {
public:
  MOCK_METHOD(absl::optional<absl::string_view>, getMetadataString,
              (absl::string_view ns, absl::string_view key), (override));
  MOCK_METHOD(absl::optional<double>, getMetadataNumber,
              (absl::string_view ns, absl::string_view key), (override));
  MOCK_METHOD(void, setMetadata,
              (absl::string_view ns, absl::string_view key, absl::string_view value), (override));
  MOCK_METHOD(void, setMetadata, (absl::string_view ns, absl::string_view key, double value),
              (override));
  MOCK_METHOD(absl::optional<absl::string_view>, getAttributeString, (AttributeID id), (override));
  MOCK_METHOD(absl::optional<uint64_t>, getAttributeNumber, (AttributeID id), (override));
  MOCK_METHOD(absl::optional<absl::string_view>, getFilterState, (absl::string_view key),
              (override));
  MOCK_METHOD(void, setFilterState, (absl::string_view key, absl::string_view value), (override));
  MOCK_METHOD(void, sendLocalResponse,
              (uint32_t status, absl::Span<const HeaderView> headers, absl::string_view body,
               absl::string_view detail),
              (override));
  MOCK_METHOD(void, sendResponseHeaders, (absl::Span<const HeaderView> headers, bool end_stream),
              (override));
  MOCK_METHOD(void, sendResponseData, (absl::string_view body, bool end_stream), (override));
  MOCK_METHOD(void, sendResponseTrailers, (absl::Span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, addCustomFlag, (absl::string_view flag), (override));
  MOCK_METHOD(void, continueRequest, (), (override));
  MOCK_METHOD(void, continueResponse, (), (override));
  MOCK_METHOD(void, clearRouteCache, (), (override));
  MOCK_METHOD(HeaderMap&, requestHeaders, (), (override));
  MOCK_METHOD(BodyBuffer&, bufferedRequestBody, (), (override));
  MOCK_METHOD(HeaderMap&, requestTrailers, (), (override));
  MOCK_METHOD(HeaderMap&, responseHeaders, (), (override));
  MOCK_METHOD(BodyBuffer&, bufferedResponseBody, (), (override));
  MOCK_METHOD(HeaderMap&, responseTrailers, (), (override));
  MOCK_METHOD(const RouteSpecificConfig*, getMostSpecificConfig, (), (override));
  MOCK_METHOD(std::shared_ptr<Scheduler>, getScheduler, (), (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), httpCallout,
              (absl::string_view cluster, absl::Span<const HeaderView> headers,
               absl::string_view body, uint64_t timeout_ms, HttpCalloutCallback& cb),
              (override));
  MOCK_METHOD((std::pair<HttpCalloutInitResult, uint64_t>), startHttpStream,
              (absl::string_view cluster, absl::Span<const HeaderView> headers,
               absl::string_view body, bool end_of_stream, uint64_t timeout_ms,
               HttpStreamCallback& cb),
              (override));
  MOCK_METHOD(bool, sendHttpStreamData,
              (uint64_t stream_id, absl::string_view body, bool end_of_stream), (override));
  MOCK_METHOD(bool, sendHttpStreamTrailers,
              (uint64_t stream_id, absl::Span<const HeaderView> trailers), (override));
  MOCK_METHOD(void, resetHttpStream, (uint64_t stream_id), (override));
  MOCK_METHOD(void, setDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, clearDownstreamWatermarkCallbacks, (), (override));
  MOCK_METHOD(MetricsResult, recordHistogramValue,
              (MetricID id, uint64_t value, absl::Span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, setGaugeValue,
              (MetricID id, uint64_t value, absl::Span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, incrementGaugeValue,
              (MetricID id, uint64_t value, absl::Span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, decrementGaugeValue,
              (MetricID id, uint64_t value, absl::Span<const BufferView> tags_values), (override));
  MOCK_METHOD(MetricsResult, incrementCounterValue,
              (MetricID id, uint64_t value, absl::Span<const BufferView> tags_values), (override));
  MOCK_METHOD(bool, logEnabled, (LogLevel level), (override));
  MOCK_METHOD(void, log, (LogLevel level, absl::string_view message), (override));
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
};

} // namespace DynamicModules
} // namespace Envoy
