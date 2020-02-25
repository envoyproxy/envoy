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
#include "envoy/ssl/connection.h"

#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

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
  MOCK_METHOD(void, onGoAway, ());
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
  MOCK_METHOD(void, dispatch, (Buffer::Instance & data));
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
  MOCK_METHOD(void, dispatch, (Buffer::Instance & data));
  MOCK_METHOD(void, goAway, ());
  MOCK_METHOD(Protocol, protocol, ());
  MOCK_METHOD(void, shutdownNotice, ());
  MOCK_METHOD(bool, wantsToWrite, ());
  MOCK_METHOD(void, onUnderlyingConnectionAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onUnderlyingConnectionBelowWriteBufferLowWatermark, ());

  // Http::ClientConnection
  MOCK_METHOD(RequestEncoder&, newStream, (ResponseDecoder & response_decoder));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory() override;

  // Http::FilterChainFactory
  MOCK_METHOD(void, createFilterChain, (FilterChainFactoryCallbacks & callbacks));
  MOCK_METHOD(bool, createUpgradeFilterChain,
              (absl::string_view upgrade_type, const FilterChainFactory::UpgradeMap* upgrade_map,
               FilterChainFactoryCallbacks& callbacks));
};

class MockStreamFilterCallbacksBase {
public:
  Event::MockDispatcher dispatcher_;
  testing::NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  std::shared_ptr<Router::MockRoute> route_;
  std::shared_ptr<Upstream::MockClusterInfo> cluster_info_;
};

class MockStreamDecoderFilterCallbacks : public StreamDecoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamDecoderFilterCallbacks();
  ~MockStreamDecoderFilterCallbacks() override;

  // Http::StreamFilterCallbacks
  MOCK_METHOD(const Network::Connection*, connection, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfo, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(void, requestRouteConfigUpdate, (Http::RouteConfigUpdatedCallbackSharedPtr));
  MOCK_METHOD(absl::optional<Router::ConfigConstSharedPtr>, routeConfig, ());
  MOCK_METHOD(void, clearRouteCache, ());
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(Tracing::Config&, tracingConfig, ());
  MOCK_METHOD(const ScopeTrackedObject&, scope, ());
  MOCK_METHOD(void, onDecoderFilterAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onDecoderFilterBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, addDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&));
  MOCK_METHOD(void, removeDownstreamWatermarkCallbacks, (DownstreamWatermarkCallbacks&));
  MOCK_METHOD(void, setDecoderBufferLimit, (uint32_t));
  MOCK_METHOD(uint32_t, decoderBufferLimit, ());
  MOCK_METHOD(bool, recreateStream, ());
  MOCK_METHOD(void, addUpstreamSocketOptions, (const Network::Socket::OptionsSharedPtr& options));
  MOCK_METHOD(Network::Socket::OptionsSharedPtr, getUpstreamSocketOptions, (), (const));

  // Http::StreamDecoderFilterCallbacks
  void sendLocalReply_(Code code, absl::string_view body,
                       std::function<void(ResponseHeaderMap& headers)> modify_headers,
                       const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
                       absl::string_view details);

  void encode100ContinueHeaders(ResponseHeaderMapPtr&& headers) override {
    encode100ContinueHeaders_(*headers);
  }
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) override {
    encodeHeaders_(*headers, end_stream);
  }
  void encodeTrailers(ResponseTrailerMapPtr&& trailers) override { encodeTrailers_(*trailers); }
  void encodeMetadata(MetadataMapPtr&& metadata_map) override {
    encodeMetadata_(std::move(metadata_map));
  }

  MOCK_METHOD(void, continueDecoding, ());
  MOCK_METHOD(void, addDecodedData, (Buffer::Instance & data, bool streaming));
  MOCK_METHOD(void, injectDecodedDataToFilterChain, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(HeaderMap&, addDecodedTrailers, ());
  MOCK_METHOD(MetadataMapVector&, addDecodedMetadata, ());
  MOCK_METHOD(const Buffer::Instance*, decodingBuffer, ());
  MOCK_METHOD(void, modifyDecodingBuffer, (std::function<void(Buffer::Instance&)>));
  MOCK_METHOD(void, encode100ContinueHeaders_, (HeaderMap & headers));
  MOCK_METHOD(void, encodeHeaders_, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(void, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(void, encodeTrailers_, (ResponseTrailerMap & trailers));
  MOCK_METHOD(void, encodeMetadata_, (MetadataMapPtr metadata_map));
  MOCK_METHOD(void, sendLocalReply,
              (Code code, absl::string_view body,
               std::function<void(ResponseHeaderMap& headers)> modify_headers,
               const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
               absl::string_view details));

  Buffer::InstancePtr buffer_;
  std::list<DownstreamWatermarkCallbacks*> callbacks_{};
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  testing::NiceMock<MockScopedTrackedObject> scope_;
  std::string details_;
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
  MOCK_METHOD(const Network::Connection*, connection, ());
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(void, resetStream, ());
  MOCK_METHOD(Upstream::ClusterInfoConstSharedPtr, clusterInfo, ());
  MOCK_METHOD(void, requestRouteConfigUpdate, (std::function<void()>));
  MOCK_METHOD(bool, canRequestRouteConfigUpdate, ());
  MOCK_METHOD(Router::RouteConstSharedPtr, route, ());
  MOCK_METHOD(void, clearRouteCache, ());
  MOCK_METHOD(uint64_t, streamId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(Tracing::Span&, activeSpan, ());
  MOCK_METHOD(Tracing::Config&, tracingConfig, ());
  MOCK_METHOD(const ScopeTrackedObject&, scope, ());
  MOCK_METHOD(void, onEncoderFilterAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onEncoderFilterBelowWriteBufferLowWatermark, ());
  MOCK_METHOD(void, setEncoderBufferLimit, (uint32_t));
  MOCK_METHOD(uint32_t, encoderBufferLimit, ());

  // Http::StreamEncoderFilterCallbacks
  MOCK_METHOD(void, addEncodedData, (Buffer::Instance & data, bool streaming));
  MOCK_METHOD(void, injectEncodedDataToFilterChain, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(HeaderMap&, addEncodedTrailers, ());
  MOCK_METHOD(void, addEncodedMetadata, (Http::MetadataMapPtr &&));
  MOCK_METHOD(void, continueEncoding, ());
  MOCK_METHOD(const Buffer::Instance*, encodingBuffer, ());
  MOCK_METHOD(void, modifyEncodingBuffer, (std::function<void(Buffer::Instance&)>));

  Buffer::InstancePtr buffer_;
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  testing::NiceMock<MockScopedTrackedObject> scope_;
};

class MockStreamDecoderFilter : public StreamDecoderFilter {
public:
  MockStreamDecoderFilter();
  ~MockStreamDecoderFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onDestroy, ());

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks));
  MOCK_METHOD(void, decodeComplete, ());

  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

class MockStreamEncoderFilter : public StreamEncoderFilter {
public:
  MockStreamEncoderFilter();
  ~MockStreamEncoderFilter() override;

  // Http::StreamFilterBase
  MOCK_METHOD(void, onDestroy, ());

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(FilterHeadersStatus, encode100ContinueHeaders, (ResponseHeaderMap & headers));
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
  MOCK_METHOD(void, onDestroy, ());

  // Http::StreamDecoderFilter
  MOCK_METHOD(FilterHeadersStatus, decodeHeaders, (RequestHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, decodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, decodeTrailers, (RequestTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, decodeMetadata, (Http::MetadataMap & metadata_map));
  MOCK_METHOD(void, setDecoderFilterCallbacks, (StreamDecoderFilterCallbacks & callbacks));

  // Http::MockStreamEncoderFilter
  MOCK_METHOD(FilterHeadersStatus, encode100ContinueHeaders, (ResponseHeaderMap & headers));
  MOCK_METHOD(FilterHeadersStatus, encodeHeaders, (ResponseHeaderMap & headers, bool end_stream));
  MOCK_METHOD(FilterDataStatus, encodeData, (Buffer::Instance & data, bool end_stream));
  MOCK_METHOD(FilterTrailersStatus, encodeTrailers, (ResponseTrailerMap & trailers));
  MOCK_METHOD(FilterMetadataStatus, encodeMetadata, (MetadataMap & metadata_map));
  MOCK_METHOD(void, setEncoderFilterCallbacks, (StreamEncoderFilterCallbacks & callbacks));

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

  void onSuccess(ResponseMessagePtr&& response) override { onSuccess_(response.get()); }

  // Http::AsyncClient::Callbacks
  MOCK_METHOD(void, onSuccess_, (ResponseMessage * response));
  MOCK_METHOD(void, onFailure, (Http::AsyncClient::FailureReason reason));
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
};

class MockFilterChainFactoryCallbacks : public Http::FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks() override;

  MOCK_METHOD(void, addStreamDecoderFilter, (Http::StreamDecoderFilterSharedPtr filter));
  MOCK_METHOD(void, addStreamEncoderFilter, (Http::StreamEncoderFilterSharedPtr filter));
  MOCK_METHOD(void, addStreamFilter, (Http::StreamFilterSharedPtr filter));
  MOCK_METHOD(void, addAccessLogHandler, (AccessLog::InstanceSharedPtr handler));
};

class MockDownstreamWatermarkCallbacks : public DownstreamWatermarkCallbacks {
public:
  MOCK_METHOD(void, onAboveWriteBufferHighWatermark, ());
  MOCK_METHOD(void, onBelowWriteBufferLowWatermark, ());
};

} // namespace Http

namespace Http {

template <typename HeaderMapT>
class HeaderValueOfMatcherImpl : public testing::MatcherInterface<HeaderMapT> {
public:
  explicit HeaderValueOfMatcherImpl(LowerCaseString key,
                                    testing::Matcher<absl::string_view> matcher)
      : key_(std::move(key)), matcher_(std::move(matcher)) {}

  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Get all headers with matching keys.
    std::vector<absl::string_view> values;
    std::pair<std::string, std::vector<absl::string_view>*> context =
        std::make_pair(key_.get(), &values);
    Envoy::Http::HeaderMap::ConstIterateCb get_headers_cb =
        [](const Envoy::Http::HeaderEntry& header, void* context) {
          auto* typed_context =
              static_cast<std::pair<std::string, std::vector<absl::string_view>*>*>(context);
          if (header.key().getStringView() == typed_context->first) {
            typed_context->second->push_back(header.value().getStringView());
          }
          return Envoy::Http::HeaderMap::Iterate::Continue;
        };
    headers.iterate(get_headers_cb, &context);

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

template <typename HeaderMapT>
class IsSubsetOfHeadersMatcherImpl : public testing::MatcherInterface<HeaderMapT> {
public:
  explicit IsSubsetOfHeadersMatcherImpl(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSubsetOfHeadersMatcherImpl(IsSubsetOfHeadersMatcherImpl&& other) noexcept
      : expected_headers_(other.expected_headers_) {}

  IsSubsetOfHeadersMatcherImpl(const IsSubsetOfHeadersMatcherImpl& other)
      : expected_headers_(other.expected_headers_) {}

  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Collect header maps into vectors, to use for IsSubsetOf.
    auto get_headers_cb = [](const HeaderEntry& header, void* headers) {
      static_cast<std::vector<std::pair<absl::string_view, absl::string_view>>*>(headers)
          ->push_back(std::make_pair(header.key().getStringView(), header.value().getStringView()));
      return HeaderMap::Iterate::Continue;
    };
    std::vector<std::pair<absl::string_view, absl::string_view>> arg_headers_vec;
    headers.iterate(get_headers_cb, &arg_headers_vec);
    std::vector<std::pair<absl::string_view, absl::string_view>> expected_headers_vec;
    expected_headers_.iterate(get_headers_cb, &expected_headers_vec);

    return ExplainMatchResult(testing::IsSubsetOf(expected_headers_vec), arg_headers_vec, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is a subset of headers:\n" << expected_headers_;
  }

  const TestHeaderMapImpl expected_headers_;
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
  TestHeaderMapImpl expected_headers_;
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

  bool MatchAndExplain(HeaderMapT headers, testing::MatchResultListener* listener) const override {
    // Collect header maps into vectors, to use for IsSupersetOf.
    auto get_headers_cb = [](const HeaderEntry& header, void* headers) {
      static_cast<std::vector<std::pair<absl::string_view, absl::string_view>>*>(headers)
          ->push_back(std::make_pair(header.key().getStringView(), header.value().getStringView()));
      return HeaderMap::Iterate::Continue;
    };
    std::vector<std::pair<absl::string_view, absl::string_view>> arg_headers_vec;
    headers.iterate(get_headers_cb, &arg_headers_vec);
    std::vector<std::pair<absl::string_view, absl::string_view>> expected_headers_vec;
    expected_headers_.iterate(get_headers_cb, &expected_headers_vec);

    return ExplainMatchResult(testing::IsSupersetOf(expected_headers_vec), arg_headers_vec,
                              listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is a superset of headers:\n" << expected_headers_;
  }

  const TestHeaderMapImpl expected_headers_;
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
  TestHeaderMapImpl expected_headers_;
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

MATCHER_P(HeaderMapEqualRef, rhs, "") { return arg == *rhs; }

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
