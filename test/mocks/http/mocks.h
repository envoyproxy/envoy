#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/http/async_client.h"
#include "envoy/http/codec.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/matcher/matcher.h"
#include "envoy/ssl/connection.h"

#include "source/common/http/conn_manager_config.h"
#include "source/common/http/filter_manager.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "test/mocks/common.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/http/conn_pool.h"
#include "test/mocks/http/stream.h"
#include "test/mocks/http/stream_decoder.h"
#include "test/mocks/http/stream_encoder.h"
#include "test/mocks/router/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/tracing/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/printers.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Http {

class MockConnectionCallbacks : public virtual ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks() override;

  // Http::ConnectionCallbacks
  MOCK_METHOD(void, onGoAway, (GoAwayErrorCode error_code), (override));
  MOCK_METHOD(void, onSettings, (ReceivedSettings & settings), (override));
  MOCK_METHOD(void, onMetadata, (MetadataMapPtr && metadata_map), (override));
};

class MockFilterManagerCallbacks : public FilterManagerCallbacks {
public:
  MockFilterManagerCallbacks();
  ~MockFilterManagerCallbacks() override;

  MOCK_METHOD(void, encodeHeaders, (ResponseHeaderMap&, bool), (override));
  MOCK_METHOD(void, encode1xxHeaders, (ResponseHeaderMap&), (override));
  MOCK_METHOD(void, encodeData, (Buffer::Instance&, bool), (override));
  MOCK_METHOD(void, encodeTrailers, (ResponseTrailerMap&), (override));
  MOCK_METHOD(void, encodeMetadata, (MetadataMapPtr &&), (override));
  MOCK_METHOD(void, chargeStats, (const ResponseHeaderMap&), (override));
  MOCK_METHOD(void, setRequestTrailers, (RequestTrailerMapPtr &&), (override));
  MOCK_METHOD(void, setInformationalHeaders_, (ResponseHeaderMap&));
  void setInformationalHeaders(ResponseHeaderMapPtr&& informational_headers) override {
    informational_headers_ = std::move(informational_headers);
    setInformationalHeaders_(*informational_headers_);
  }
  MOCK_METHOD(void, setResponseHeaders_, (ResponseHeaderMap&));
  void setResponseHeaders(ResponseHeaderMapPtr&& response_headers) override {
    response_headers_ = std::move(response_headers);
    setResponseHeaders_(*response_headers_);
  }
  MOCK_METHOD(void, setResponseTrailers_, (ResponseTrailerMap&));
  void setResponseTrailers(ResponseTrailerMapPtr&& response_trailers) override {
    response_trailers_ = std::move(response_trailers);
    setResponseTrailers_(*response_trailers_);
  }
  MOCK_METHOD(RequestHeaderMapOptRef, requestHeaders, (), (override));
  MOCK_METHOD(RequestTrailerMapOptRef, requestTrailers, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (override));
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (override));
  MOCK_METHOD(void, endStream, (), (override));
  MOCK_METHOD(void, sendGoAwayAndClose, (bool graceful), (override));
  MOCK_METHOD(void, onDecoderFilterBelowWriteBufferLowWatermark, (), (override));
  MOCK_METHOD(void, onDecoderFilterAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, disarmRequestTimeout, (), (override));
  MOCK_METHOD(void, resetIdleTimer, (), (override));
  MOCK_METHOD(void, recreateStream, (StreamInfo::FilterStateSharedPtr filter_state), (override));
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason),
              (override));
  MOCK_METHOD(const Router::RouteEntry::UpgradeMap*, upgradeMap, (), (override));
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, clusterInfo, (), (override));
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfoSharedPtr, (), (override));
  MOCK_METHOD(OptRef<const Router::Route>, route, (const Router::RouteCallback& cb));
  MOCK_METHOD(Router::RouteConstSharedPtr, routeSharedPtr, (const Router::RouteCallback& cb));
  MOCK_METHOD(void, setRoute, (Router::RouteConstSharedPtr));
  MOCK_METHOD(void, clearRouteCache, ());
  MOCK_METHOD(std::optional<Router::ConfigConstSharedPtr>, routeConfig, ());
  MOCK_METHOD(void, requestRouteConfigUpdate, (Http::RouteConfigUpdatedCallbackSharedPtr));
  MOCK_METHOD(Tracing::Span&, activeSpan, (), (override));
  MOCK_METHOD(void, onResponseDataTooLarge, (), (override));
  MOCK_METHOD(void, onRequestDataTooLarge, (), (override));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, (), (override));
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, (), (override));
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, (), (override));
  MOCK_METHOD(void, onLocalReply, (Code code), (override));
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const, override));
  MOCK_METHOD(const ScopeTrackedObject&, scope, (), (override));
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&));
  MOCK_METHOD(bool, isHalfCloseEnabled, (), (override));

  ResponseHeaderMapPtr informational_headers_;
  ResponseHeaderMapPtr response_headers_;
  ResponseTrailerMapPtr response_trailers_;
};

class MockServerConnectionCallbacks : public ServerConnectionCallbacks,
                                      public MockConnectionCallbacks {
public:
  MockServerConnectionCallbacks();
  ~MockServerConnectionCallbacks() override;

  // Http::ServerConnectionCallbacks
  MOCK_METHOD(RequestDecoder&, newStream,
              (ResponseEncoder & response_encoder, bool is_internally_created), (override));
};

class MockStreamCallbacks : public StreamCallbacks {
public:
  MockStreamCallbacks();
  ~MockStreamCallbacks() override;

  // Http::StreamCallbacks
  MOCK_METHOD(void, onResetStream, (StreamResetReason reason, absl::string_view), (override));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
};

class MockCodecEventCallbacks : public CodecEventCallbacks {
public:
  MockCodecEventCallbacks();
  ~MockCodecEventCallbacks() override;

  MOCK_METHOD(void, onCodecEncodeComplete, (), (override));
  MOCK_METHOD(void, onCodecLowLevelReset, (), (override));
};

class MockServerConnection : public ServerConnection {
public:
  MockServerConnection();
  ~MockServerConnection() override;

  // Http::Connection
  MOCK_METHOD(Status, dispatch, (Buffer::Instance & data), (override));
  MOCK_METHOD(void, goAway, (), (override));
  MOCK_METHOD(Protocol, protocol, (), (override));
  MOCK_METHOD(void, shutdownNotice, (), (override));
  MOCK_METHOD(bool, wantsToWrite, (), (override));
  MOCK_METHOD(void, onUnderlyingConnectionAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onUnderlyingConnectionBelowWriteBufferLowWatermark, (), (override));

  Protocol protocol_{Protocol::Http11};
};

class MockClientConnection : public ClientConnection {
public:
  MockClientConnection();
  ~MockClientConnection() override;

  // Http::Connection
  MOCK_METHOD(Status, dispatch, (Buffer::Instance & data), (override));
  MOCK_METHOD(void, goAway, (), (override));
  MOCK_METHOD(Protocol, protocol, (), (override));
  MOCK_METHOD(void, shutdownNotice, (), (override));
  MOCK_METHOD(bool, wantsToWrite, (), (override));
  MOCK_METHOD(void, onUnderlyingConnectionAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onUnderlyingConnectionBelowWriteBufferLowWatermark, (), (override));

  // Http::ClientConnection
  MOCK_METHOD(RequestEncoder&, newStream, (ResponseDecoder & response_decoder), (override));
};

class MockFilterChainFactoryCallbacks : public Http::FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addStreamDecoderFilter, (Http::StreamDecoderFilterSharedPtr filter),
              (override));
  MOCK_METHOD(void, addStreamEncoderFilter, (Http::StreamEncoderFilterSharedPtr filter),
              (override));
  MOCK_METHOD(void, addStreamFilter, (Http::StreamFilterSharedPtr filter), (override));
  MOCK_METHOD(void, addAccessLogHandler, (AccessLog::InstanceSharedPtr handler), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const, override));
  MOCK_METHOD(void, setFilterConfigName, (absl::string_view name), (override));
  MOCK_METHOD(OptRef<const Router::Route>, route, (), (const, override));
  MOCK_METHOD(std::optional<bool>, filterDisabled, (absl::string_view filter_name),
              (const, override));
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const, override));
  MOCK_METHOD(RequestHeaderMapOptRef, requestHeaders, (), (const, override));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  // Http::FilterChainFactory
  MOCK_METHOD(bool, createFilterChain, (FilterChainFactoryCallbacks & callbacks),
              (const, override));
  MOCK_METHOD(bool, createUpgradeFilterChain,
              (absl::string_view upgrade_type, const FilterChainFactory::UpgradeMap* upgrade_map,
               FilterChainFactoryCallbacks& callbacks),
              (const, override));
};

class MockStreamFilterCallbacksBase {
public:
  NiceMock<Event::MockDispatcher> dispatcher_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_;
};

class MockDownstreamStreamFilterCallbacks : public DownstreamStreamFilterCallbacks {
public:
  ~MockDownstreamStreamFilterCallbacks() override = default;

  MOCK_METHOD(OptRef<const Router::Route>, route, (const Router::RouteCallback&), (override));
  MOCK_METHOD(Router::RouteConstSharedPtr, routeSharedPtr, (const Router::RouteCallback&),
              (override));
  MOCK_METHOD(void, setRoute, (Router::RouteConstSharedPtr), (override));
  MOCK_METHOD(void, requestRouteConfigUpdate, (Http::RouteConfigUpdatedCallbackSharedPtr),
              (override));
  MOCK_METHOD(void, clearRouteCache, (), (override));
  MOCK_METHOD(void, refreshRouteConfigSnapshot, (), (override));
  MOCK_METHOD(void, refreshRouteCluster, (), (override));
  MOCK_METHOD(void, recreateClusterInfo, (), (override));

  std::shared_ptr<Router::MockRoute> route_;
};

class MockSidestreamWatermarkCallbacks : public SidestreamWatermarkCallbacks {
public:
  ~MockSidestreamWatermarkCallbacks() override = default;

  MOCK_METHOD(void, onSidestreamAboveHighWatermark, (), (override));
  MOCK_METHOD(void, onSidestreamBelowLowWatermark, (), (override));
  MOCK_METHOD(void, addDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&), (override));
  MOCK_METHOD(void, removeDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&),
              (override));
};

class MockStreamDecoderFilterCallbacks : public StreamDecoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamDecoderFilterCallbacks();
  ~MockStreamDecoderFilterCallbacks() override;

  // Http::StreamFilterCallbacks
  MOCK_METHOD(OptRef<const Network::Connection>, connection, (), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason),
              (override));
  MOCK_METHOD(void, resetIdleTimer, (), (override));
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, clusterInfo, (), (override));
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfoSharedPtr, (), (override));
  MOCK_METHOD(OptRef<const Router::Route>, route, (), (override));
  MOCK_METHOD(Router::RouteConstSharedPtr, routeSharedPtr, (), (override));
  MOCK_METHOD(std::optional<Router::ConfigConstSharedPtr>, routeConfig, ());
  MOCK_METHOD(uint64_t, streamId, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(Tracing::Span&, activeSpan, (), (override));
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const, override));
  MOCK_METHOD(const ScopeTrackedObject&, scope, (), (override));
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&), (override));
  MOCK_METHOD(void, onDecoderFilterAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onDecoderFilterBelowWriteBufferLowWatermark, (), (override));
  MOCK_METHOD(void, addDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&), (override));
  MOCK_METHOD(void, removeDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&),
              (override));
  MOCK_METHOD(void, addUpstreamWatermarkCallbacks, (UpstreamWatermarkCallbacks&), (override));
  MOCK_METHOD(void, removeUpstreamWatermarkCallbacks, (UpstreamWatermarkCallbacks&), (override));
  MOCK_METHOD(void, setBufferLimit, (uint64_t), (override));
  MOCK_METHOD(uint64_t, bufferLimit, (), (override));
  MOCK_METHOD(bool, recreateStream, (const ResponseHeaderMap* headers), (override));
  MOCK_METHOD(void, sendGoAwayAndClose, (bool graceful), (override));
  MOCK_METHOD(void, addUpstreamSocketOptions, (const Network::Socket::OptionsSharedPtr& options),
              (override));
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, getUpstreamSocketOptions, (), (const, override));
  MOCK_METHOD(const Router::RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (),
              (const, override));
  MOCK_METHOD(Router::RouteSpecificFilterConfigs, perFilterConfigs, (), (const, override));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, (), (override));
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, (), (override));
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, (), (override));
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const override));
  MOCK_METHOD(RequestHeaderMapOptRef, requestHeaders, (), (override));
  MOCK_METHOD(RequestTrailerMapOptRef, requestTrailers, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (override));
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (override));

  // Http::StreamDecoderFilterCallbacks
  // NOLINTNEXTLINE(readability-identifier-naming)
  void sendLocalReply_(Code code, absl::string_view body,
                       std::function<void(ResponseHeaderMap& headers)> modify_headers,
                       const std::optional<Grpc::Status::GrpcStatus> grpc_status,
                       absl::string_view details);

  void encode1xxHeaders(ResponseHeaderMapPtr&& headers) override { encode1xxHeaders_(*headers); }
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override {
    stream_info_.setResponseCodeDetails(details);
    encodeHeaders_(*headers, end_stream);
  }
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override { encodeTrailers_(*trailers); }
  void encodeMetadata(MetadataMapPtr&& metadata_map) override {
    encodeMetadata_(std::move(metadata_map));
  }
  absl::string_view details() {
    if (stream_info_.responseCodeDetails()) {
      return stream_info_.responseCodeDetails().value();
    }
    return "";
  }

  MOCK_METHOD(void, continueDecoding, (), (override));
  MOCK_METHOD(void, addDecodedData, (Buffer::Instance & data, bool streaming), (override));
  MOCK_METHOD(void, injectDecodedDataToFilterChain, (Buffer::Instance & data, bool end_stream),
              (override));
  MOCK_METHOD(void, injectDecodedHeadersToFilterChain, (bool end_stream), (override));
  MOCK_METHOD(RequestTrailerMap&, addDecodedTrailers, (), (override));
  MOCK_METHOD(MetadataMapVector&, addDecodedMetadata, (), (override));
  MOCK_METHOD(const Buffer::Instance*, decodingBuffer, (), (override));
  MOCK_METHOD(void, modifyDecodingBuffer, (std::function<void(Buffer::Instance&)>), (override));
  MOCK_METHOD(void, encode1xxHeaders_, (HeaderMap & headers));
  MOCK_METHOD(void, encodeHeaders_, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, encodeTrailers_, (ResponseTrailerMap & trailers));
  MOCK_METHOD(void, encodeMetadata_, (MetadataMapPtr metadata_map));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               std::function<void(ResponseHeaderMap& headers)> modify_headers,
               const std::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details),
              (override));
  MOCK_METHOD(Buffer::BufferMemoryAccountSharedPtr, account, (), (const, override));
  MOCK_METHOD(void, setUpstreamOverrideHost, (Upstream::LoadBalancerContext::OverrideHost),
              (override));
  MOCK_METHOD(OptRef<const Upstream::LoadBalancerContext::OverrideHost>, upstreamOverrideHost, (),
              (const, override));
  MOCK_METHOD(bool, shouldLoadShed, (), (const, override));

  Buffer::InstancePtr buffer_;
  std::list<DownstreamWatermarkCallbacks*> callbacks_;
  testing::NiceMock<MockDownstreamStreamFilterCallbacks> downstream_callbacks_;
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  testing::NiceMock<MockScopeTrackedObject> scope_;
  bool is_grpc_request_{};
  bool is_head_request_{false};
  bool stream_destroyed_{};
  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockStreamEncoderFilterCallbacks : public StreamEncoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamEncoderFilterCallbacks();
  ~MockStreamEncoderFilterCallbacks() override;

  // Http::StreamFilterCallbacks
  MOCK_METHOD(OptRef<const Network::Connection>, connection, (), (override));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason),
              (override));
  MOCK_METHOD(void, resetIdleTimer, (), (override));
  MOCK_METHOD(OptRef<const Upstream::ClusterInfo>, clusterInfo, (), (override));
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfoSharedPtr, (), (override));
  MOCK_METHOD(OptRef<const Router::Route>, route, (), (override));
  MOCK_METHOD(Router::RouteConstSharedPtr, routeSharedPtr, (), (override));
  MOCK_METHOD(bool, canRequestRouteConfigUpdate, ());
  MOCK_METHOD(uint64_t, streamId, (), (const, override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));
  MOCK_METHOD(Tracing::Span&, activeSpan, (), (override));
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const, override));
  MOCK_METHOD(const ScopeTrackedObject&, scope, (), (override));
  MOCK_METHOD(void, onEncoderFilterAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onEncoderFilterBelowWriteBufferLowWatermark, (), (override));
  MOCK_METHOD(void, setBufferLimit, (uint64_t), (override));
  MOCK_METHOD(uint64_t, bufferLimit, (), (override));
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&), (override));
  MOCK_METHOD(const Router::RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (),
              (const, override));
  MOCK_METHOD(Router::RouteSpecificFilterConfigs, perFilterConfigs, (), (const, override));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, (), (override));
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, (), (override));
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, (), (override));
  MOCK_METHOD(absl::string_view, filterConfigName, (), (const override));
  MOCK_METHOD(RequestHeaderMapOptRef, requestHeaders, (), (override));
  MOCK_METHOD(RequestTrailerMapOptRef, requestTrailers, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (override));
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (override));

  // Http::StreamEncoderFilterCallbacks
  MOCK_METHOD(void, addEncodedData, (Buffer::Instance & data, bool streaming), (override));
  MOCK_METHOD(void, injectEncodedDataToFilterChain, (Buffer::Instance & data, bool end_stream),
              (override));
  MOCK_METHOD(void, injectEncodedHeadersToFilterChain, (bool end_stream), (override));
  MOCK_METHOD(ResponseTrailerMap&, addEncodedTrailers, (), (override));
  MOCK_METHOD(void, addEncodedMetadata, (Http::MetadataMapPtr &&), (override));
  MOCK_METHOD(void, continueEncoding, (), (override));
  MOCK_METHOD(const Buffer::Instance*, encodingBuffer, (), (override));
  MOCK_METHOD(void, modifyEncodingBuffer, (std::function<void(Buffer::Instance&)>), (override));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               std::function<void(ResponseHeaderMap& headers)> modify_headers,
               const std::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details),
              (override));

  Buffer::InstancePtr buffer_;
  testing::NiceMock<MockDownstreamStreamFilterCallbacks> downstream_callbacks_;
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  testing::NiceMock<MockScopeTrackedObject> scope_;
};

class MockStreamDecoderFilter : public StreamDecoderFilter {
public:
  MockStreamDecoderFilter();
  ~MockStreamDecoderFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onStreamComplete, (), (override));
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&), (override));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&), (override));

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream),
              (override));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers), (override));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map), (override));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, decodeComplete, (), (override));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
               bool is_head_request, const std::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));

  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

class MockStreamEncoderFilter : public StreamEncoderFilter {
public:
  MockStreamEncoderFilter();
  ~MockStreamEncoderFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onStreamComplete, (), (override));
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&), (override));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&), (override));

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(Filter1xxHeadersStatus, encode1xxHeaders, (ResponseHeaderMap & headers), (override));
  MOCK_METHOD(FilterHeadersStatus, encodeHeaders, (ResponseHeaderMap & headers, bool end_stream),
              (override));
  MOCK_METHOD(FilterDataStatus, encodeData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterTrailersStatus, encodeTrailers, (ResponseTrailerMap & trailers), (override));
  MOCK_METHOD(FilterMetadataStatus, encodeMetadata, (MetadataMap & metadata_map), (override));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (StreamEncoderFilterCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, encodeComplete, (), (override));

  Http::StreamEncoderFilterCallbacks* callbacks_{};
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();
  ~MockStreamFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onStreamComplete, (), (override));
  MOCK_METHOD(void, onDestroy, (), (override));
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&), (override));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&), (override));

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream),
              (override));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers), (override));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map), (override));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, decodeComplete, (), (override));

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(Filter1xxHeadersStatus, encode1xxHeaders, (ResponseHeaderMap & headers), (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (const));
  MOCK_METHOD(FilterHeadersStatus, encodeHeaders, (ResponseHeaderMap & headers, bool end_stream),
              (override));
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (const));
  MOCK_METHOD(FilterDataStatus, encodeData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(FilterTrailersStatus, encodeTrailers, (ResponseTrailerMap & trailers), (override));
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (const));
  MOCK_METHOD(FilterMetadataStatus, encodeMetadata, (MetadataMap & metadata_map), (override));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (StreamEncoderFilterCallbacks & callbacks),
              (override));
  MOCK_METHOD(void, encodeComplete, (), (override));

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

class MockAsyncClient : public AsyncClient {
public:
  MockAsyncClient();
  ~MockAsyncClient() override;

  MOCK_METHOD(void, onRequestDestroy, ());

  // Http::AsyncClient
  Request* send(RequestMessagePtr&& request, Callbacks& callbacks,
                const RequestOptions& args) override {
    return send_(request, callbacks, args);
  }
  OngoingRequest* startRequest(RequestHeaderMapPtr&& headers, Callbacks& callbacks,
                               const RequestOptions& args) override {
    return startRequest_(headers, callbacks, args);
  }
  MOCK_METHOD(Request*, send_,
              (RequestMessagePtr & request, Callbacks& callbacks, const RequestOptions& args));
  MOCK_METHOD(OngoingRequest*, startRequest_,
              (RequestHeaderMapPtr & request, Callbacks& callbacks, const RequestOptions& args));
  MOCK_METHOD(Stream*, start, (StreamCallbacks & callbacks, const StreamOptions& args), (override));

  MOCK_METHOD(Event::Dispatcher&, dispatcher, (), (override));

  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockAsyncClientCallbacks : public AsyncClient::Callbacks {
public:
  MockAsyncClientCallbacks();
  ~MockAsyncClientCallbacks() override;

  void onSuccess(const Http::AsyncClient::Request& request,
                 ResponseMessagePtr&& response) override {
    onSuccess_(request, response.get());
  }

  // Http::AsyncClient::Callbacks
  MOCK_METHOD(void, onSuccess_, (const Http::AsyncClient::Request&, ResponseMessage*));
  MOCK_METHOD(void, onFailure,
              (const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason), (override));
  MOCK_METHOD(void, onBeforeFinalizeUpstreamSpan,
              (Envoy::Tracing::Span&, const Http::ResponseHeaderMap*), (override));
};

class MockAsyncClientStreamCallbacks : public AsyncClient::StreamCallbacks {
public:
  MockAsyncClientStreamCallbacks();
  ~MockAsyncClientStreamCallbacks() override;

  void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    onHeaders_(*headers, end_stream);
  }
  void onTrailers(ResponseTrailerMapPtr&& trailers) override { onTrailers_(*trailers); }

  MOCK_METHOD(void, onHeaders_, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(void, onData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, onTrailers_, (ResponseTrailerMap & headers));
  MOCK_METHOD(void, onComplete, (), (override));
  MOCK_METHOD(void, onReset, (), (override));
};

class MockAsyncClientRequest : public virtual AsyncClient::Request {
public:
  MockAsyncClientRequest(MockAsyncClient* client);
  ~MockAsyncClientRequest() override;

  MOCK_METHOD(void, cancel, (), (override));

  MockAsyncClient* client_;
};

class MockAsyncClientStream : public virtual AsyncClient::Stream {
public:
  MockAsyncClientStream();
  ~MockAsyncClientStream() override;

  MOCK_METHOD(void, sendHeaders, (RequestHeaderMap & headers, bool end_stream), (override));
  MOCK_METHOD(void, sendData, (Buffer::Instance & data, bool end_stream), (override));
  MOCK_METHOD(void, sendTrailers, (RequestTrailerMap & trailers), (override));
  MOCK_METHOD(void, reset, (), (override));
  MOCK_METHOD(bool, isAboveWriteBufferHighWatermark, (), (const, override));
  void setDestructorCallback(AsyncClient::StreamDestructorCallbacks callback) override {
    destructor_callback_ = callback;
  }
  void removeDestructorCallback() override { destructor_callback_.reset(); }
  MOCK_METHOD(void, setWatermarkCallbacks, (Http::SidestreamWatermarkCallbacks & callback),
              (override));
  MOCK_METHOD(void, removeWatermarkCallbacks, (), (override));
  MOCK_METHOD(const StreamInfo::StreamInfo&, streamInfo, (), (const override));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, (), (override));

private:
  std::optional<AsyncClient::StreamDestructorCallbacks> destructor_callback_;
};

class MockAsyncClientOngoingRequest : public virtual AsyncClient::OngoingRequest,
                                      public MockAsyncClientStream,
                                      public MockAsyncClientRequest {
public:
  MockAsyncClientOngoingRequest(MockAsyncClient* client) : MockAsyncClientRequest(client) {}
  void captureAndSendTrailers(RequestTrailerMapPtr&& trailers) override {
    return captureAndSendTrailers_(*trailers);
  }
  MOCK_METHOD(void, captureAndSendTrailers_, (RequestTrailerMap & trailers), ());
};

class MockDownstreamWatermarkCallbacks : public DownstreamWatermarkCallbacks {
public:
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
};

class MockUpstreamWatermarkCallbacks : public UpstreamWatermarkCallbacks {
public:
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, (), (override));
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, (), (override));
};

class MockConnectionManagerConfig : public ConnectionManagerConfig {
public:
  MockConnectionManagerConfig() {
    ON_CALL(*this, generateRequestId()).WillByDefault(testing::Return(true));
    ON_CALL(*this, isRoutable()).WillByDefault(testing::Return(true));
    ON_CALL(*this, preserveExternalRequestId()).WillByDefault(testing::Return(false));
    ON_CALL(*this, alwaysSetRequestIdInResponse()).WillByDefault(testing::Return(false));
    ON_CALL(*this, schemeToSet()).WillByDefault(testing::ReturnRef(scheme_));
    ON_CALL(*this, shouldSchemeMatchUpstream())
        .WillByDefault(testing::ReturnPointee(&scheme_match_upstream_));
    ON_CALL(*this, addProxyProtocolConnectionState()).WillByDefault(testing::Return(true));
    ON_CALL(*this, httpsDestinationPorts())
        .WillByDefault(testing::ReturnRef(https_destination_ports_));
    ON_CALL(*this, httpDestinationPorts())
        .WillByDefault(testing::ReturnRef(http_destination_ports_));
  }

  // Http::ConnectionManagerConfig
  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance& instance,
                                  ServerConnectionCallbacks& callbacks,
                                  Server::OverloadManager& overload_manager) override {
    return ServerConnectionPtr{createCodec_(connection, instance, callbacks, overload_manager)};
  }

  MOCK_METHOD(const RequestIDExtensionSharedPtr&, requestIDExtension, (), (override));
  MOCK_METHOD(const AccessLog::InstanceSharedPtrVector&, accessLogs, (), (override));
  MOCK_METHOD(bool, flushAccessLogOnNewRequest, (), (override));
  MOCK_METHOD(bool, flushAccessLogOnTunnelSuccessfullyEstablished, (), (const, override));
  MOCK_METHOD(const std::optional<std::chrono::milliseconds>&, accessLogFlushInterval, (),
              (override));
  MOCK_METHOD(ServerConnection*, createCodec_,
              (Network::Connection&, const Buffer::Instance&, ServerConnectionCallbacks&,
               Server::OverloadManager&));
  MOCK_METHOD(DateProvider&, dateProvider, (), (override));
  MOCK_METHOD(std::chrono::milliseconds, drainTimeout, (), (const, override));
  MOCK_METHOD(FilterChainFactory&, filterFactory, (), (override));
  MOCK_METHOD(bool, generateRequestId, (), (const, override));
  MOCK_METHOD(bool, preserveExternalRequestId, (), (const, override));
  MOCK_METHOD(bool, alwaysSetRequestIdInResponse, (), (const, override));
  MOCK_METHOD(uint32_t, maxRequestHeadersKb, (), (const, override));
  MOCK_METHOD(uint32_t, maxRequestHeadersCount, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, idleTimeout, (), (const, override));
  MOCK_METHOD(bool, isRoutable, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, maxConnectionDuration, (),
              (const, override));
  MOCK_METHOD(bool, http1SafeMaxConnectionDuration, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, maxStreamDuration, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, streamIdleTimeout, (), (const, override));
  MOCK_METHOD(std::optional<std::chrono::milliseconds>, streamFlushTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, requestTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, requestHeadersTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, delayedCloseTimeout, (), (const, override));
  MOCK_METHOD(Router::RouteConfigProvider*, routeConfigProvider, (), (override));
  MOCK_METHOD(Config::ConfigProvider*, scopedRouteConfigProvider, (), (override));
  MOCK_METHOD(OptRef<const Router::ScopeKeyBuilder>, scopeKeyBuilder, (), (override));
  MOCK_METHOD(const std::string&, serverName, (), (const, override));
  MOCK_METHOD(HttpConnectionManagerProto::ServerHeaderTransformation, serverHeaderTransformation,
              (), (const, override));
  MOCK_METHOD(const std::optional<std::string>&, schemeToSet, (), (const, override));
  MOCK_METHOD(bool, shouldSchemeMatchUpstream, (), (const, override));
  MOCK_METHOD(ConnectionManagerStats&, stats, (), (override));
  MOCK_METHOD(ConnectionManagerTracingStats&, tracingStats, (), (override));
  MOCK_METHOD(bool, useRemoteAddress, (), (const, override));
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return *internal_address_config_;
  }

  MOCK_METHOD(bool, unixSocketInternal, ());
  MOCK_METHOD(uint32_t, xffNumTrustedHops, (), (const, override));
  MOCK_METHOD(bool, skipXffAppend, (), (const, override));
  MOCK_METHOD(const std::string&, via, (), (const, override));
  MOCK_METHOD(Http::ForwardClientCertType, forwardClientCert, (), (const, override));
  MOCK_METHOD(const std::vector<Http::ClientCertDetailsType>&, setCurrentClientCertDetails, (),
              (const, override));
  MOCK_METHOD(Http::ClientCertFormat, clientCertFormat, (), (const, override));
  MOCK_METHOD(const Matcher::MatchTreePtr<HttpMatchingData>&, forwardClientCertMatcher, (),
              (const, override));
  MOCK_METHOD(const Network::Address::Instance&, localAddress, (), (override));
  MOCK_METHOD(const std::optional<std::string>&, userAgent, (), (override));
  MOCK_METHOD(const Http::TracingConnectionManagerConfig*, tracingConfig, (), (override));
  MOCK_METHOD(Tracing::TracerSharedPtr, tracer, (), (override));
  MOCK_METHOD(ConnectionManagerListenerStats&, listenerStats, (), (override));
  MOCK_METHOD(bool, proxy100Continue, (), (const, override));
  MOCK_METHOD(bool, streamErrorOnInvalidHttpMessaging, (), (const, override));
  MOCK_METHOD(const Http::Http1Settings&, http1Settings, (), (const, override));
  MOCK_METHOD(bool, shouldNormalizePath, (), (const, override));
  MOCK_METHOD(bool, shouldMergeSlashes, (), (const, override));
  MOCK_METHOD(bool, shouldStripTrailingHostDot, (), (const, override));
  MOCK_METHOD(bool, recordRouteResolutionStats, (), (const, override));
  MOCK_METHOD(Http::StripPortType, stripPortType, (), (const, override));
  MOCK_METHOD(envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction,
              headersWithUnderscoresAction, (), (const, override));
  MOCK_METHOD(const LocalReply::LocalReply&, localReply, (), (const, override));
  MOCK_METHOD(envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager::PathWithEscapedSlashesAction,
              pathWithEscapedSlashesAction, (), (const, override));
  MOCK_METHOD(const std::vector<Http::OriginalIPDetectionSharedPtr>&, originalIpDetectionExtensions,
              (), (const, override));
  const std::vector<Http::EarlyHeaderMutationPtr>& earlyHeaderMutationExtensions() const override {
    return early_header_mutation_extensions_;
  }
  MOCK_METHOD(uint32_t, maxRequestsPerConnection, (), (const, override));
  MOCK_METHOD(const HttpConnectionManagerProto::ProxyStatusConfig*, proxyStatusConfig, (),
              (const, override));
  MOCK_METHOD(ServerHeaderValidatorPtr, makeHeaderValidator, (Protocol protocol), (override));
  MOCK_METHOD(bool, appendLocalOverload, (), (const, override));
  MOCK_METHOD(bool, appendXForwardedPort, (), (const, override));
  MOCK_METHOD(bool, addProxyProtocolConnectionState, (), (const, override));
  MOCK_METHOD((const absl::flat_hash_set<uint32_t>&), httpsDestinationPorts, (), (const, override));
  MOCK_METHOD((const absl::flat_hash_set<uint32_t>&), httpDestinationPorts, (), (const, override));

  class AllowInternalAddressConfig : public Http::InternalAddressConfig {
  public:
    bool isInternalAddress(const Network::Address::Instance& address) const override {
      return Network::Utility::isInternalAddress(address);
    }
  };

  std::unique_ptr<Http::InternalAddressConfig> internal_address_config_ =
      std::make_unique<AllowInternalAddressConfig>();
  std::vector<Http::EarlyHeaderMutationPtr> early_header_mutation_extensions_;
  std::optional<std::string> scheme_;
  bool scheme_match_upstream_;
  absl::flat_hash_set<uint32_t> https_destination_ports_;
  absl::flat_hash_set<uint32_t> http_destination_ports_;
};

class MockReceivedSettings : public ReceivedSettings {
public:
  MockReceivedSettings();
  ~MockReceivedSettings() override = default;

  MOCK_METHOD(const std::optional<uint32_t>&, maxConcurrentStreams, (), (const, override));

  std::optional<uint32_t> max_concurrent_streams_;
};

} // namespace Http

namespace Http {

template <typename HeaderMapT>
class HeaderValueOfMatcherImpl : public testing::MatcherInterface<HeaderMapT> {
public:
  explicit HeaderValueOfMatcherImpl(LowerCaseString key,
                                    testing::Matcher<absl::string_view> matcher)
      : key_(std::move(key)), matcher_(std::move(matcher)) {}

  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Get all headers with matching keys.
    std::vector<absl::string_view> values;
    Envoy::Http::HeaderMap::ConstIterateCb get_headers_cb =
        [key = key_.get(), &values](const Envoy::Http::HeaderEntry& header) {
          if (header.key().getStringView() == key) {
            values.push_back(header.value().getStringView());
          }
          return Envoy::Http::HeaderMap::Iterate::Continue;
        };
    headers.iterate(get_headers_cb);

    if (values.empty()) {
      *listener << "which has no '" << key_.get() << "' header";
      return false;
    } else if (values.size() > 1) {
      *listener << "which has " << values.size() << " '" << key_.get()
                << "' headers, with values: " << absl::StrJoin(values, ", ");
      return false;
    }
    absl::string_view value = values[0];
    *listener << "which has a '" << key_.get() << "' header with value " << value << " ";
    return testing::ExplainMatchResult(matcher_, value, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "has a '" << key_.get() << "' header with value that "
        << testing::DescribeMatcher<absl::string_view>(matcher_);
  }

  void DescribeNegationTo(std::ostream* os) const override {
    *os << "doesn't have a '" << key_.get() << "' header with value that "
        << testing::DescribeMatcher<absl::string_view>(matcher_);
  }

private:
  const LowerCaseString key_;
  const testing::Matcher<absl::string_view> matcher_;
};

class HeaderValueOfMatcher {
public:
  explicit HeaderValueOfMatcher(LowerCaseString key, testing::Matcher<absl::string_view> matcher)
      : key_(std::move(key)), matcher_(std::move(matcher)) {}

  // Produces a testing::Matcher that is parameterized by HeaderMap& or const
  // HeaderMap& as requested. This is required since testing::Matcher<const T&>
  // is not implicitly convertible to testing::Matcher<T&>.
  template <typename HeaderMapT> operator testing::Matcher<HeaderMapT>() const {
    return testing::Matcher<HeaderMapT>(new HeaderValueOfMatcherImpl<HeaderMapT>(key_, matcher_));
  }

private:
  const LowerCaseString key_;
  const testing::Matcher<absl::string_view> matcher_;
};

// Tests the provided Envoy HeaderMap for the provided HTTP status code.
MATCHER_P(HttpStatusIs, expected_code, "") {
  const HeaderEntry* status = arg.Status();
  if (status == nullptr) {
    *result_listener << "which has no status code";
    return false;
  }
  const absl::string_view code = status->value().getStringView();
  if (code != absl::StrCat(expected_code)) {
    *result_listener << "which has status code " << code;
    return false;
  }
  return true;
}

inline HeaderMap::ConstIterateCb
saveHeaders(std::vector<std::pair<absl::string_view, absl::string_view>>* output) {
  return [output](const HeaderEntry& header) {
    output->push_back(std::make_pair(header.key().getStringView(), header.value().getStringView()));
    return HeaderMap::Iterate::Continue;
  };
}

template <typename HeaderMapT>
class IsSubsetOfHeadersMatcherImpl : public testing::MatcherInterface<HeaderMapT> {
public:
  explicit IsSubsetOfHeadersMatcherImpl(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSubsetOfHeadersMatcherImpl(IsSubsetOfHeadersMatcherImpl&& other) noexcept
      : expected_headers_(other.expected_headers_) {}

  IsSubsetOfHeadersMatcherImpl(const IsSubsetOfHeadersMatcherImpl& other)
      : expected_headers_(other.expected_headers_) {}

  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Collect header maps into vectors, to use for IsSubsetOf.
    std::vector<std::pair<absl::string_view, absl::string_view>> arg_headers_vec;
    headers.iterate(saveHeaders(&arg_headers_vec));

    std::vector<std::pair<absl::string_view, absl::string_view>> expected_headers_vec;
    expected_headers_.iterate(saveHeaders(&expected_headers_vec));

    return ExplainMatchResult(testing::IsSubsetOf(expected_headers_vec), arg_headers_vec, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is a subset of headers:\n" << expected_headers_;
  }

  const TestRequestHeaderMapImpl expected_headers_;
};

class IsSubsetOfHeadersMatcher {
public:
  IsSubsetOfHeadersMatcher(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSubsetOfHeadersMatcher(IsSubsetOfHeadersMatcher&& other) noexcept
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  IsSubsetOfHeadersMatcher(const IsSubsetOfHeadersMatcher& other)
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  template <typename HeaderMapT> operator testing::Matcher<HeaderMapT>() const {
    return testing::MakeMatcher(new IsSubsetOfHeadersMatcherImpl<HeaderMapT>(expected_headers_));
  }

private:
  TestRequestHeaderMapImpl expected_headers_;
};

IsSubsetOfHeadersMatcher IsSubsetOfHeaders(const HeaderMap& expected_headers);

template <typename HeaderMapT>
class IsSupersetOfHeadersMatcherImpl : public testing::MatcherInterface<HeaderMapT> {
public:
  explicit IsSupersetOfHeadersMatcherImpl(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSupersetOfHeadersMatcherImpl(IsSupersetOfHeadersMatcherImpl&& other) noexcept
      : expected_headers_(other.expected_headers_) {}

  IsSupersetOfHeadersMatcherImpl(const IsSupersetOfHeadersMatcherImpl& other)
      : expected_headers_(other.expected_headers_) {}

  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Collect header maps into vectors, to use for IsSupersetOf.
    std::vector<std::pair<absl::string_view, absl::string_view>> arg_headers_vec;
    headers.iterate(saveHeaders(&arg_headers_vec));

    std::vector<std::pair<absl::string_view, absl::string_view>> expected_headers_vec;
    expected_headers_.iterate(saveHeaders(&expected_headers_vec));

    if (!ExplainMatchResult(testing::IsSupersetOf(expected_headers_vec), arg_headers_vec,
                            listener)) {
      *listener << "\nActual headers:\n" << headers;
      return false;
    }
    return true;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is a superset of headers:\n" << expected_headers_;
  }

  const TestRequestHeaderMapImpl expected_headers_;
};

class IsSupersetOfHeadersMatcher {
public:
  IsSupersetOfHeadersMatcher(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSupersetOfHeadersMatcher(IsSupersetOfHeadersMatcher&& other) noexcept
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  IsSupersetOfHeadersMatcher(const IsSupersetOfHeadersMatcher& other)
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  template <typename HeaderMapT> operator testing::Matcher<HeaderMapT>() const {
    return testing::MakeMatcher(new IsSupersetOfHeadersMatcherImpl<HeaderMapT>(expected_headers_));
  }

private:
  TestRequestHeaderMapImpl expected_headers_;
};

IsSupersetOfHeadersMatcher IsSupersetOfHeaders(const HeaderMap& expected_headers);

} // namespace Http

MATCHER_P(HeaderMapEqual, rhs, "") {
  const bool equal = (*arg == *rhs);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("header map:") << "\n"
                     << *rhs << TestUtility::addLeftAndRightPadding("is not equal to:") << "\n"
                     << *arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(HeaderMapEqualWithMaxSize, rhs, "") {
  bool equal = (*arg == *rhs);

  // Check the max header count and size of the HeaderMap also equal.
  if (equal) {
    if (arg->maxHeadersCount() != rhs->maxHeadersCount() ||
        arg->maxHeadersKb() != rhs->maxHeadersKb()) {
      equal = false;
    }
  }

  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("header map:") << "\n"
                     << *rhs << TestUtility::addLeftAndRightPadding("is not equal to:") << "\n"
                     << *arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

MATCHER_P(HeaderMapEqualRef, rhs, "") {
  const bool equal = (arg == *rhs);
  if (!equal) {
    *result_listener << "\n"
                     << TestUtility::addLeftAndRightPadding("header map:") << "\n"
                     << *rhs << TestUtility::addLeftAndRightPadding("is not equal to:") << "\n"
                     << arg << TestUtility::addLeftAndRightPadding("") // line full of padding
                     << "\n";
  }
  return equal;
}

// Test that a HeaderMap& argument includes a given key-value pair, e.g.,
// ContainsHeader("Upgrade", "WebSocket"). Key is case-insensitive.
// Value can be a matcher, e.g.
// ContainsHeader("Upgrade", HasSubstr("Socket"))
template <typename K, typename V> Http::HeaderValueOfMatcher ContainsHeader(K key, V value) {
  return Http::HeaderValueOfMatcher(Http::LowerCaseString(key),
                                    testing::SafeMatcherCast<absl::string_view>(value));
}

} // namespace Envoy
