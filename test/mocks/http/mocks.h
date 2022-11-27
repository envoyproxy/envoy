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

using testing::Return;

namespace Envoy {
namespace Http {

class MockConnectionCallbacks : public virtual ConnectionCallbacks {
public:
  MockConnectionCallbacks();
  ~MockConnectionCallbacks() override;

  // Http::ConnectionCallbacks
  MOCK_METHOD(void, onGoAway, (GoAwayErrorCode error_code));
  MOCK_METHOD(void, onSettings, (ReceivedSettings & settings));
};

class MockFilterManagerCallbacks : public FilterManagerCallbacks {
public:
  MockFilterManagerCallbacks();
  ~MockFilterManagerCallbacks() override;

  MOCK_METHOD(void, encodeHeaders, (ResponseHeaderMap&, bool));
  MOCK_METHOD(void, encode1xxHeaders, (ResponseHeaderMap&));
  MOCK_METHOD(void, encodeData, (Buffer::Instance&, bool));
  MOCK_METHOD(void, encodeTrailers, (ResponseTrailerMap&));
  MOCK_METHOD(void, encodeMetadata, (MetadataMapPtr &&));
  MOCK_METHOD(void, chargeStats, (const ResponseHeaderMap&));
  MOCK_METHOD(void, setRequestTrailers, (RequestTrailerMapPtr &&));
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
  MOCK_METHOD(RequestHeaderMapOptRef, requestHeaders, ());
  MOCK_METHOD(RequestTrailerMapOptRef, requestTrailers, ());
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, ());
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, ());
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, ());
  MOCK_METHOD(void, endStream, ());
  MOCK_METHOD(void, onDecoderFilterBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, onDecoderFilterAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, upgradeFilterChainCreated, ());
  MOCK_METHOD(void, disarmRequestTimeout, ());
  MOCK_METHOD(void, resetIdleTimer, ());
  MOCK_METHOD(void, recreateStream, (StreamInfo::FilterStateSharedPtr filter_state));
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason));
  MOCK_METHOD(const Router::RouteEntry::UpgradeMap*, upgradeMap, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfo, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, (const Router::RouteCallback& cb));
  MOCK_METHOD(void, setRoute, (Router::RouteConstSharedPtr));
  MOCK_METHOD(void, clearRouteCache, ());
  MOCK_METHOD(absl::optional<Router::ConfigConstSharedPtr>, routeConfig, ());
  MOCK_METHOD(void, requestRouteConfigUpdate, (Http::RouteConfigUpdatedCallbackSharedPtr));
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(void, onResponseDataTooLarge, ());
  MOCK_METHOD(void, onRequestDataTooLarge, ());
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, ());
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, ());
  MOCK_METHOD(void, onLocalReply, (Code code));
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const));
  MOCK_METHOD(const ScopeTrackedObject&, scope, ());
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&));

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
              (ResponseEncoder & response_encoder, bool is_internally_created));
};

class MockStreamCallbacks : public StreamCallbacks {
public:
  MockStreamCallbacks();
  ~MockStreamCallbacks() override;

  // Http::StreamCallbacks
  MOCK_METHOD(void, onResetStream, (StreamResetReason reason, absl::string_view));
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockServerConnection : public ServerConnection {
public:
  MockServerConnection();
  ~MockServerConnection() override;

  // Http::Connection
  MOCK_METHOD(Status, dispatch, (Buffer::Instance & data));
  MOCK_METHOD(void, goAway, ());
  MOCK_METHOD(Protocol, protocol, ());
  MOCK_METHOD(void, shutdownNotice, ());
  MOCK_METHOD(bool, wantsToWrite, ());
  MOCK_METHOD(void, onUnderlyingConnectionAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onUnderlyingConnectionBelowWriteBufferLowWatermark, ());

  Protocol protocol_{Protocol::Http11};
};

class MockClientConnection : public ClientConnection {
public:
  MockClientConnection();
  ~MockClientConnection() override;

  // Http::Connection
  MOCK_METHOD(Status, dispatch, (Buffer::Instance & data));
  MOCK_METHOD(void, goAway, ());
  MOCK_METHOD(Protocol, protocol, ());
  MOCK_METHOD(void, shutdownNotice, ());
  MOCK_METHOD(bool, wantsToWrite, ());
  MOCK_METHOD(void, onUnderlyingConnectionAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onUnderlyingConnectionBelowWriteBufferLowWatermark, ());

  // Http::ClientConnection
  MOCK_METHOD(RequestEncoder&, newStream, (ResponseDecoder & response_decoder));
};

class MockFilterChainFactoryCallbacks : public Http::FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addStreamDecoderFilter, (Http::StreamDecoderFilterSharedPtr filter));
  MOCK_METHOD(void, addStreamEncoderFilter, (Http::StreamEncoderFilterSharedPtr filter));
  MOCK_METHOD(void, addStreamFilter, (Http::StreamFilterSharedPtr filter));
  MOCK_METHOD(void, addAccessLogHandler, (AccessLog::InstanceSharedPtr handler));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class MockFilterChainManager : public FilterChainManager {
public:
  MockFilterChainManager();
  ~MockFilterChainManager() override;

  // Http::FilterChainManager
  MOCK_METHOD(void, applyFilterFactoryCb, (FilterContext context, FilterFactoryCb& factory));

  NiceMock<MockFilterChainFactoryCallbacks> callbacks_;
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  // Http::FilterChainFactory
  bool createFilterChain(FilterChainManager& manager, bool) const override {
    return createFilterChain(manager);
  }
  MOCK_METHOD(bool, createFilterChain, (FilterChainManager & manager), (const));
  MOCK_METHOD(bool, createUpgradeFilterChain,
              (absl::string_view upgrade_type, const FilterChainFactory::UpgradeMap* upgrade_map,
               FilterChainManager& manager),
              (const));
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

  MOCK_METHOD(Router::RouteConstSharedPtr, route, (const Router::RouteCallback&));
  MOCK_METHOD(void, setRoute, (Router::RouteConstSharedPtr));
  MOCK_METHOD(void, requestRouteConfigUpdate, (Http::RouteConfigUpdatedCallbackSharedPtr));
  MOCK_METHOD(void, clearRouteCache, ());

  std::shared_ptr<Router::MockRoute> route_;
};

class MockStreamDecoderFilterCallbacks : public StreamDecoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamDecoderFilterCallbacks();
  ~MockStreamDecoderFilterCallbacks() override;

  // Http::StreamFilterCallbacks
  MOCK_METHOD(OptRef<const Network::Connection>, connection, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason));
  MOCK_METHOD(void, resetIdleTimer, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfo, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(absl::optional<Router::ConfigConstSharedPtr>, routeConfig, ());
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const));
  MOCK_METHOD(const ScopeTrackedObject&, scope, ());
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&));
  MOCK_METHOD(void, onDecoderFilterAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onDecoderFilterBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, addDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&));
  MOCK_METHOD(void, removeDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&));
  MOCK_METHOD(void, setDecoderBufferLimit, (uint32_t));
  MOCK_METHOD(uint32_t, decoderBufferLimit, ());
  MOCK_METHOD(bool, recreateStream, (const ResponseHeaderMap* headers));
  MOCK_METHOD(void, addUpstreamSocketOptions, (const Network::Socket::OptionsSharedPtr& options));
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, getUpstreamSocketOptions, (), (const));
  MOCK_METHOD(const Router::RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (), (const));
  MOCK_METHOD(void, traversePerFilterConfig,
              (std::function<void(const Router::RouteSpecificFilterConfig&)> cb), (const));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, ());
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, ());

  // Http::StreamDecoderFilterCallbacks
  // NOLINTNEXTLINE(readability-identifier-naming)
  void sendLocalReply_(Code code, absl::string_view body,
                       std::function<void(ResponseHeaderMap& headers)> modify_headers,
                       const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                       absl::string_view details);

  void encode1xxHeaders(ResponseHeaderMapPtr&& headers) override { encode1xxHeaders_(*headers); }
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (const));
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream,
                     absl::string_view details) override {
    stream_info_.setResponseCodeDetails(details);
    encodeHeaders_(*headers, end_stream);
  }
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (const));
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override { encodeTrailers_(*trailers); }
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (const));
  void encodeMetadata(MetadataMapPtr&& metadata_map) override {
    encodeMetadata_(std::move(metadata_map));
  }
  absl::string_view details() {
    if (stream_info_.responseCodeDetails()) {
      return stream_info_.responseCodeDetails().value();
    }
    return "";
  }

  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, addDecodedData, (Buffer::Instance & data, bool streaming));
  MOCK_METHOD(void, injectDecodedDataToFilterChain, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(RequestTrailerMap&, addDecodedTrailers, ());
  MOCK_METHOD(MetadataMapVector&, addDecodedMetadata, ());
  MOCK_METHOD(const Buffer::Instance*, decodingBuffer, ());
  MOCK_METHOD(void, modifyDecodingBuffer, (std::function<void(Buffer::Instance&)>));
  MOCK_METHOD(void, encode1xxHeaders_, (HeaderMap & headers));
  MOCK_METHOD(void, encodeHeaders_, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeTrailers_, (ResponseTrailerMap & trailers));
  MOCK_METHOD(void, encodeMetadata_, (MetadataMapPtr metadata_map));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               std::function<void(ResponseHeaderMap& headers)> modify_headers,
               const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));
  MOCK_METHOD(Buffer::BufferMemoryAccountSharedPtr, account, (), (const));
  MOCK_METHOD(void, setUpstreamOverrideHost, (absl::string_view host));
  MOCK_METHOD(absl::optional<absl::string_view>, upstreamOverrideHost, (), (const));

  Buffer::InstancePtr buffer_;
  std::list<DownstreamWatermarkCallbacks*> callbacks_{};
  testing::NiceMock<MockDownstreamStreamFilterCallbacks> downstream_callbacks_;
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  testing::NiceMock<MockScopeTrackedObject> scope_;
  bool is_grpc_request_{};
  bool is_head_request_{false};
  bool stream_destroyed_{};
};

class MockStreamEncoderFilterCallbacks : public StreamEncoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamEncoderFilterCallbacks();
  ~MockStreamEncoderFilterCallbacks() override;

  // Http::StreamFilterCallbacks
  MOCK_METHOD(OptRef<const Network::Connection>, connection, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, resetStream,
              (Http::StreamResetReason reset_reason, absl::string_view transport_failure_reason));
  MOCK_METHOD(void, resetIdleTimer, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfo, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(bool, canRequestRouteConfigUpdate, ());
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(OptRef<const Tracing::Config>, tracingConfig, (), (const));
  MOCK_METHOD(const ScopeTrackedObject&, scope, ());
  MOCK_METHOD(void, onEncoderFilterAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onEncoderFilterBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, setEncoderBufferLimit, (uint32_t));
  MOCK_METHOD(uint32_t, encoderBufferLimit, ());
  MOCK_METHOD(void, restoreContextOnContinue, (ScopeTrackedObjectStack&));
  MOCK_METHOD(const Router::RouteSpecificFilterConfig*, mostSpecificPerFilterConfig, (), (const));
  MOCK_METHOD(void, traversePerFilterConfig,
              (std::function<void(const Router::RouteSpecificFilterConfig&)> cb), (const));
  MOCK_METHOD(Http1StreamEncoderOptionsOptRef, http1StreamEncoderOptions, ());
  MOCK_METHOD(OptRef<DownstreamStreamFilterCallbacks>, downstreamCallbacks, ());
  MOCK_METHOD(OptRef<UpstreamStreamFilterCallbacks>, upstreamCallbacks, ());

  // Http::StreamEncoderFilterCallbacks
  MOCK_METHOD(void, addEncodedData, (Buffer::Instance & data, bool streaming));
  MOCK_METHOD(void, injectEncodedDataToFilterChain, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(ResponseTrailerMap&, addEncodedTrailers, ());
  MOCK_METHOD(void, addEncodedMetadata, (Http::MetadataMapPtr &&));
  MOCK_METHOD(void, continueEncoding, ());
  MOCK_METHOD(const Buffer::Instance*, encodingBuffer, ());
  MOCK_METHOD(void, modifyEncodingBuffer, (std::function<void(Buffer::Instance&)>));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               std::function<void(ResponseHeaderMap& headers)> modify_headers,
               const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));

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
  MOCK_METHOD(void, onStreamComplete, ());
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&));

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, decodeComplete, ());
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               const std::function<void(ResponseHeaderMap& headers)>& modify_headers,
               bool is_head_request, const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));

  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

class MockStreamEncoderFilter : public StreamEncoderFilter {
public:
  MockStreamEncoderFilter();
  ~MockStreamEncoderFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onStreamComplete, ());
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&));

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(FilterHeadersStatus, encode1xxHeaders, (ResponseHeaderMap & headers));
  MOCK_METHOD(FilterHeadersStatus, encodeHeaders, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, encodeTrailers, (ResponseTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, encodeMetadata, (MetadataMap & metadata_map));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (StreamEncoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, encodeComplete, ());

  Http::StreamEncoderFilterCallbacks* callbacks_{};
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();
  ~MockStreamFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onStreamComplete, ());
  MOCK_METHOD(void, onDestroy, ());
  MOCK_METHOD(void, onMatchCallback, (const Matcher::Action&));
  MOCK_METHOD(LocalErrorStatus, onLocalReply, (const LocalReplyData&));

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, decodeComplete, ());

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(FilterHeadersStatus, encode1xxHeaders, (ResponseHeaderMap & headers));
  MOCK_METHOD(ResponseHeaderMapOptRef, informationalHeaders, (), (const));
  MOCK_METHOD(FilterHeadersStatus, encodeHeaders, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(ResponseHeaderMapOptRef, responseHeaders, (), (const));
  MOCK_METHOD(FilterDataStatus, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, encodeTrailers, (ResponseTrailerMap & trailers));
  MOCK_METHOD(ResponseTrailerMapOptRef, responseTrailers, (), (const));
  MOCK_METHOD(FilterMetadataStatus, encodeMetadata, (MetadataMap & metadata_map));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (StreamEncoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, encodeComplete, ());

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

  MOCK_METHOD(Request*, send_,
              (RequestMessagePtr & request, Callbacks& callbacks, const RequestOptions& args));

  MOCK_METHOD(Stream*, start, (StreamCallbacks & callbacks, const StreamOptions& args));

  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());

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
              (const Http::AsyncClient::Request&, Http::AsyncClient::FailureReason));
  MOCK_METHOD(void, onBeforeFinalizeUpstreamSpan,
              (Envoy::Tracing::Span&, const Http::ResponseHeaderMap*));
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
  MOCK_METHOD(void, onData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, onTrailers_, (ResponseTrailerMap & headers));
  MOCK_METHOD(void, onComplete, ());
  MOCK_METHOD(void, onReset, ());
};

class MockAsyncClientRequest : public AsyncClient::Request {
public:
  MockAsyncClientRequest(MockAsyncClient* client);
  ~MockAsyncClientRequest() override;

  MOCK_METHOD(void, cancel, ());

  MockAsyncClient* client_;
};

class MockAsyncClientStream : public AsyncClient::Stream {
public:
  MockAsyncClientStream();
  ~MockAsyncClientStream() override;

  MOCK_METHOD(void, sendHeaders, (RequestHeaderMap & headers, bool end_stream));
  MOCK_METHOD(void, sendData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, sendTrailers, (RequestTrailerMap & trailers));
  MOCK_METHOD(void, reset, ());
  MOCK_METHOD(bool, isAboveWriteBufferHighWatermark, (), (const));
};

class MockDownstreamWatermarkCallbacks : public DownstreamWatermarkCallbacks {
public:
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

class MockConnectionManagerConfig : public ConnectionManagerConfig {
public:
  MockConnectionManagerConfig() {
    ON_CALL(*this, generateRequestId()).WillByDefault(testing::Return(true));
    ON_CALL(*this, isRoutable()).WillByDefault(testing::Return(true));
    ON_CALL(*this, preserveExternalRequestId()).WillByDefault(testing::Return(false));
    ON_CALL(*this, alwaysSetRequestIdInResponse()).WillByDefault(testing::Return(false));
    ON_CALL(*this, schemeToSet()).WillByDefault(testing::ReturnRef(scheme_));
  }

  // Http::ConnectionManagerConfig
  ServerConnectionPtr createCodec(Network::Connection& connection, const Buffer::Instance& instance,
                                  ServerConnectionCallbacks& callbacks) override {
    return ServerConnectionPtr{createCodec_(connection, instance, callbacks)};
  }

  MOCK_METHOD(const RequestIDExtensionSharedPtr&, requestIDExtension, ());
  MOCK_METHOD(const std::list<AccessLog::InstanceSharedPtr>&, accessLogs, ());
  MOCK_METHOD(ServerConnection*, createCodec_,
              (Network::Connection&, const Buffer::Instance&, ServerConnectionCallbacks&));
  MOCK_METHOD(DateProvider&, dateProvider, ());
  MOCK_METHOD(std::chrono::milliseconds, drainTimeout, (), (const));
  MOCK_METHOD(FilterChainFactory&, filterFactory, ());
  MOCK_METHOD(bool, generateRequestId, (), (const));
  MOCK_METHOD(bool, preserveExternalRequestId, (), (const));
  MOCK_METHOD(bool, alwaysSetRequestIdInResponse, (), (const));
  MOCK_METHOD(uint32_t, maxRequestHeadersKb, (), (const));
  MOCK_METHOD(uint32_t, maxRequestHeadersCount, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, idleTimeout, (), (const));
  MOCK_METHOD(bool, isRoutable, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxConnectionDuration, (), (const));
  MOCK_METHOD(absl::optional<std::chrono::milliseconds>, maxStreamDuration, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, streamIdleTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, requestTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, requestHeadersTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, delayedCloseTimeout, (), (const));
  MOCK_METHOD(Router::RouteConfigProvider*, routeConfigProvider, ());
  MOCK_METHOD(Config::ConfigProvider*, scopedRouteConfigProvider, ());
  MOCK_METHOD(const std::string&, serverName, (), (const));
  MOCK_METHOD(HttpConnectionManagerProto::ServerHeaderTransformation, serverHeaderTransformation,
              (), (const));
  MOCK_METHOD(const absl::optional<std::string>&, schemeToSet, (), (const));
  MOCK_METHOD(ConnectionManagerStats&, stats, ());
  MOCK_METHOD(ConnectionManagerTracingStats&, tracingStats, ());
  MOCK_METHOD(bool, useRemoteAddress, (), (const));
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return *internal_address_config_;
  }

  MOCK_METHOD(bool, unixSocketInternal, ());
  MOCK_METHOD(uint32_t, xffNumTrustedHops, (), (const));
  MOCK_METHOD(bool, skipXffAppend, (), (const));
  MOCK_METHOD(const std::string&, via, (), (const));
  MOCK_METHOD(Http::ForwardClientCertType, forwardClientCert, (), (const));
  MOCK_METHOD(const std::vector<Http::ClientCertDetailsType>&, setCurrentClientCertDetails, (),
              (const));
  MOCK_METHOD(const Network::Address::Instance&, localAddress, ());
  MOCK_METHOD(const absl::optional<std::string>&, userAgent, ());
  MOCK_METHOD(const Http::TracingConnectionManagerConfig*, tracingConfig, ());
  MOCK_METHOD(Tracing::HttpTracerSharedPtr, tracer, ());
  MOCK_METHOD(ConnectionManagerListenerStats&, listenerStats, ());
  MOCK_METHOD(bool, proxy100Continue, (), (const));
  MOCK_METHOD(bool, streamErrorOnInvalidHttpMessaging, (), (const));
  MOCK_METHOD(const Http::Http1Settings&, http1Settings, (), (const));
  MOCK_METHOD(bool, shouldNormalizePath, (), (const));
  MOCK_METHOD(bool, shouldMergeSlashes, (), (const));
  MOCK_METHOD(bool, shouldStripTrailingHostDot, (), (const));
  MOCK_METHOD(Http::StripPortType, stripPortType, (), (const));
  MOCK_METHOD(envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction,
              headersWithUnderscoresAction, (), (const));
  MOCK_METHOD(const LocalReply::LocalReply&, localReply, (), (const));
  MOCK_METHOD(envoy::extensions::filters::network::http_connection_manager::v3::
                  HttpConnectionManager::PathWithEscapedSlashesAction,
              pathWithEscapedSlashesAction, (), (const));
  MOCK_METHOD(const std::vector<Http::OriginalIPDetectionSharedPtr>&, originalIpDetectionExtensions,
              (), (const));
  MOCK_METHOD(uint64_t, maxRequestsPerConnection, (), (const));
  MOCK_METHOD(const HttpConnectionManagerProto::ProxyStatusConfig*, proxyStatusConfig, (), (const));
  MOCK_METHOD(HeaderValidatorPtr, makeHeaderValidator,
              (Protocol protocol, StreamInfo::StreamInfo& stream_info));

  std::unique_ptr<Http::InternalAddressConfig> internal_address_config_ =
      std::make_unique<DefaultInternalAddressConfig>();
  absl::optional<std::string> scheme_;
};

class MockReceivedSettings : public ReceivedSettings {
public:
  MockReceivedSettings();
  ~MockReceivedSettings() override = default;

  MOCK_METHOD(const absl::optional<uint32_t>&, maxConcurrentStreams, (), (const));

  absl::optional<uint32_t> max_concurrent_streams_{};
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

// Test that a HeaderMap argument contains exactly one header with the given
// key, whose value satisfies the given expectation. The expectation can be a
// matcher, or a string that the value should equal.
template <typename T, typename K> HeaderValueOfMatcher HeaderValueOf(K key, const T& matcher) {
  return HeaderValueOfMatcher(LowerCaseString(key),
                              testing::SafeMatcherCast<absl::string_view>(matcher));
}

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

    return ExplainMatchResult(testing::IsSupersetOf(expected_headers_vec), arg_headers_vec,
                              listener);
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

// Test that a HeaderMapPtr argument includes a given key-value pair, e.g.,
//  HeaderHasValue("Upgrade", "WebSocket")
template <typename K, typename V>
testing::Matcher<const Http::HeaderMap*> HeaderHasValue(K key, V value) {
  return testing::Pointee(Http::HeaderValueOf(key, value));
}

// Like HeaderHasValue, but matches against a HeaderMap& argument.
template <typename K, typename V> Http::HeaderValueOfMatcher HeaderHasValueRef(K key, V value) {
  return Http::HeaderValueOf(key, value);
}

} // namespace Envoy
