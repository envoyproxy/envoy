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
  ~MockConnectionCallbacks();

  // Http::ConnectionCallbacks
  MOCK_METHOD0(onGoAway, void());
};

class MockServerConnectionCallbacks : public ServerConnectionCallbacks,
                                      public MockConnectionCallbacks {
public:
  MockServerConnectionCallbacks();
  ~MockServerConnectionCallbacks();

  // Http::ServerConnectionCallbacks
  MOCK_METHOD1(newStream, StreamDecoder&(StreamEncoder& response_encoder));
};

class MockStreamDecoder : public StreamDecoder {
public:
  MockStreamDecoder();
  ~MockStreamDecoder();

  void decode100ContinueHeaders(HeaderMapPtr&& headers) override {
    decode100ContinueHeaders_(headers);
  }
  void decodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    decodeHeaders_(headers, end_stream);
  }
  void decodeTrailers(HeaderMapPtr&& trailers) override { decodeTrailers_(trailers); }

  void decodeMetadata(MetadataMapPtr&& metadata_map) override { decodeMetadata_(metadata_map); }

  // Http::StreamDecoder
  MOCK_METHOD2(decodeHeaders_, void(HeaderMapPtr& headers, bool end_stream));
  MOCK_METHOD1(decode100ContinueHeaders_, void(HeaderMapPtr& headers));
  MOCK_METHOD2(decodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers_, void(HeaderMapPtr& trailers));
  MOCK_METHOD1(decodeMetadata_, void(MetadataMapPtr& metadata_map));
};

class MockStreamCallbacks : public StreamCallbacks {
public:
  MockStreamCallbacks();
  ~MockStreamCallbacks();

  // Http::StreamCallbacks
  MOCK_METHOD1(onResetStream, void(StreamResetReason reason));
  MOCK_METHOD0(onAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onBelowWriteBufferLowWatermark, void());
};

class MockStream : public Stream {
public:
  MockStream();
  ~MockStream();

  // Http::Stream
  MOCK_METHOD1(addCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(removeCallbacks, void(StreamCallbacks& callbacks));
  MOCK_METHOD1(resetStream, void(StreamResetReason reason));
  MOCK_METHOD1(readDisable, void(bool disable));
  MOCK_METHOD2(setWriteBufferWatermarks, void(uint32_t, uint32_t));
  MOCK_METHOD0(bufferLimit, uint32_t());

  std::list<StreamCallbacks*> callbacks_{};

  void runHighWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }

  void runLowWatermarkCallbacks() {
    for (auto* callback : callbacks_) {
      callback->onBelowWriteBufferLowWatermark();
    }
  }
};

class MockStreamEncoder : public StreamEncoder {
public:
  MockStreamEncoder();
  ~MockStreamEncoder();

  // Http::StreamEncoder
  MOCK_METHOD1(encode100ContinueHeaders, void(const HeaderMap& headers));
  MOCK_METHOD2(encodeHeaders, void(const HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, void(const HeaderMap& trailers));
  MOCK_METHOD1(encodeMetadata, void(const MetadataMap& metadata_map));
  MOCK_METHOD0(getStream, Stream&());

  testing::NiceMock<MockStream> stream_;
};

class MockServerConnection : public ServerConnection {
public:
  MockServerConnection();
  ~MockServerConnection();

  // Http::Connection
  MOCK_METHOD1(dispatch, void(Buffer::Instance& data));
  MOCK_METHOD0(goAway, void());
  MOCK_METHOD0(protocol, Protocol());
  MOCK_METHOD0(shutdownNotice, void());
  MOCK_METHOD0(wantsToWrite, bool());
  MOCK_METHOD0(onUnderlyingConnectionAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onUnderlyingConnectionBelowWriteBufferLowWatermark, void());

  Protocol protocol_{Protocol::Http11};
};

class MockClientConnection : public ClientConnection {
public:
  MockClientConnection();
  ~MockClientConnection();

  // Http::Connection
  MOCK_METHOD1(dispatch, void(Buffer::Instance& data));
  MOCK_METHOD0(goAway, void());
  MOCK_METHOD0(protocol, Protocol());
  MOCK_METHOD0(shutdownNotice, void());
  MOCK_METHOD0(wantsToWrite, bool());
  MOCK_METHOD0(onUnderlyingConnectionAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onUnderlyingConnectionBelowWriteBufferLowWatermark, void());

  // Http::ClientConnection
  MOCK_METHOD1(newStream, StreamEncoder&(StreamDecoder& response_decoder));
};

class MockFilterChainFactory : public FilterChainFactory {
public:
  MockFilterChainFactory();
  ~MockFilterChainFactory();

  // Http::FilterChainFactory
  MOCK_METHOD1(createFilterChain, void(FilterChainFactoryCallbacks& callbacks));
  MOCK_METHOD2(createUpgradeFilterChain,
               bool(absl::string_view upgrade_type, FilterChainFactoryCallbacks& callbacks));
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
  ~MockStreamDecoderFilterCallbacks();

  // Http::StreamFilterCallbacks
  MOCK_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(clusterInfo, Upstream::ClusterInfoConstSharedPtr());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_METHOD0(clearRouteCache, void());
  MOCK_METHOD0(streamId, uint64_t());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_METHOD0(activeSpan, Tracing::Span&());
  MOCK_METHOD0(tracingConfig, Tracing::Config&());
  MOCK_METHOD0(onDecoderFilterAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onDecoderFilterBelowWriteBufferLowWatermark, void());
  MOCK_METHOD1(addDownstreamWatermarkCallbacks, void(DownstreamWatermarkCallbacks&));
  MOCK_METHOD1(removeDownstreamWatermarkCallbacks, void(DownstreamWatermarkCallbacks&));
  MOCK_METHOD1(setDecoderBufferLimit, void(uint32_t));
  MOCK_METHOD0(decoderBufferLimit, uint32_t());

  // Http::StreamDecoderFilterCallbacks
  void sendLocalReply(Code code, const std::string& body,
                      std::function<void(HeaderMap& headers)> modify_headers,
                      const absl::optional<Grpc::Status::GrpcStatus> grpc_status) override {
    Utility::sendLocalReply(
        is_grpc_request_,
        [this, modify_headers](HeaderMapPtr&& headers, bool end_stream) -> void {
          if (modify_headers != nullptr) {
            modify_headers(*headers);
          }
          encodeHeaders(std::move(headers), end_stream);
        },
        [this](Buffer::Instance& data, bool end_stream) -> void { encodeData(data, end_stream); },
        stream_destroyed_, code, body, grpc_status, is_head_request_);
  }
  void encode100ContinueHeaders(HeaderMapPtr&& headers) override {
    encode100ContinueHeaders_(*headers);
  }
  void encodeHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    encodeHeaders_(*headers, end_stream);
  }
  void encodeTrailers(HeaderMapPtr&& trailers) override { encodeTrailers_(*trailers); }

  MOCK_METHOD0(continueDecoding, void());
  MOCK_METHOD2(addDecodedData, void(Buffer::Instance& data, bool streaming));
  MOCK_METHOD0(addDecodedTrailers, HeaderMap&());
  MOCK_METHOD0(decodingBuffer, const Buffer::Instance*());
  MOCK_METHOD1(encode100ContinueHeaders_, void(HeaderMap& headers));
  MOCK_METHOD2(encodeHeaders_, void(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers_, void(HeaderMap& trailers));

  Buffer::InstancePtr buffer_;
  std::list<DownstreamWatermarkCallbacks*> callbacks_{};
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
  bool is_grpc_request_{};
  bool is_head_request_{false};
  bool stream_destroyed_{};
};

class MockStreamEncoderFilterCallbacks : public StreamEncoderFilterCallbacks,
                                         public MockStreamFilterCallbacksBase {
public:
  MockStreamEncoderFilterCallbacks();
  ~MockStreamEncoderFilterCallbacks();

  // Http::StreamFilterCallbacks
  MOCK_METHOD0(connection, const Network::Connection*());
  MOCK_METHOD0(dispatcher, Event::Dispatcher&());
  MOCK_METHOD0(resetStream, void());
  MOCK_METHOD0(clusterInfo, Upstream::ClusterInfoConstSharedPtr());
  MOCK_METHOD0(route, Router::RouteConstSharedPtr());
  MOCK_METHOD0(clearRouteCache, void());
  MOCK_METHOD0(streamId, uint64_t());
  MOCK_METHOD0(streamInfo, StreamInfo::StreamInfo&());
  MOCK_METHOD0(activeSpan, Tracing::Span&());
  MOCK_METHOD0(tracingConfig, Tracing::Config&());
  MOCK_METHOD0(onEncoderFilterAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onEncoderFilterBelowWriteBufferLowWatermark, void());
  MOCK_METHOD1(setEncoderBufferLimit, void(uint32_t));
  MOCK_METHOD0(encoderBufferLimit, uint32_t());

  // Http::StreamEncoderFilterCallbacks
  MOCK_METHOD2(addEncodedData, void(Buffer::Instance& data, bool streaming));
  MOCK_METHOD0(addEncodedTrailers, HeaderMap&());
  MOCK_METHOD0(continueEncoding, void());
  MOCK_METHOD0(encodingBuffer, const Buffer::Instance*());

  Buffer::InstancePtr buffer_;
  testing::NiceMock<Tracing::MockSpan> active_span_;
  testing::NiceMock<Tracing::MockConfig> tracing_config_;
};

class MockStreamDecoderFilter : public StreamDecoderFilter {
public:
  MockStreamDecoderFilter();
  ~MockStreamDecoderFilter();

  // Http::StreamFilterBase
  MOCK_METHOD0(onDestroy, void());

  // Http::StreamDecoderFilter
  MOCK_METHOD2(decodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(decodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setDecoderFilterCallbacks, void(StreamDecoderFilterCallbacks& callbacks));

  Http::StreamDecoderFilterCallbacks* callbacks_{};
};

class MockStreamEncoderFilter : public StreamEncoderFilter {
public:
  MockStreamEncoderFilter();
  ~MockStreamEncoderFilter();

  // Http::StreamFilterBase
  MOCK_METHOD0(onDestroy, void());

  // Http::MockStreamEncoderFilter
  MOCK_METHOD1(encode100ContinueHeaders, FilterHeadersStatus(HeaderMap& headers));
  MOCK_METHOD2(encodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setEncoderFilterCallbacks, void(StreamEncoderFilterCallbacks& callbacks));

  Http::StreamEncoderFilterCallbacks* callbacks_{};
};

class MockStreamFilter : public StreamFilter {
public:
  MockStreamFilter();
  ~MockStreamFilter();

  // Http::StreamFilterBase
  MOCK_METHOD0(onDestroy, void());

  // Http::StreamDecoderFilter
  MOCK_METHOD2(decodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(decodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(decodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setDecoderFilterCallbacks, void(StreamDecoderFilterCallbacks& callbacks));

  // Http::MockStreamEncoderFilter
  MOCK_METHOD1(encode100ContinueHeaders, FilterHeadersStatus(HeaderMap& headers));
  MOCK_METHOD2(encodeHeaders, FilterHeadersStatus(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(encodeData, FilterDataStatus(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(encodeTrailers, FilterTrailersStatus(HeaderMap& trailers));
  MOCK_METHOD1(setEncoderFilterCallbacks, void(StreamEncoderFilterCallbacks& callbacks));

  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
};

class MockAsyncClient : public AsyncClient {
public:
  MockAsyncClient();
  ~MockAsyncClient();

  MOCK_METHOD0(onRequestDestroy, void());

  // Http::AsyncClient
  Request* send(MessagePtr&& request, Callbacks& callbacks, const RequestOptions& args) override {
    return send_(request, callbacks, args);
  }

  MOCK_METHOD3(send_,
               Request*(MessagePtr& request, Callbacks& callbacks, const RequestOptions& args));

  MOCK_METHOD2(start, Stream*(StreamCallbacks& callbacks, const StreamOptions& args));

  MOCK_METHOD0(dispatcher, Event::Dispatcher&());

  NiceMock<Event::MockDispatcher> dispatcher_;
};

class MockAsyncClientCallbacks : public AsyncClient::Callbacks {
public:
  MockAsyncClientCallbacks();
  ~MockAsyncClientCallbacks();

  void onSuccess(MessagePtr&& response) override { onSuccess_(response.get()); }

  // Http::AsyncClient::Callbacks
  MOCK_METHOD1(onSuccess_, void(Message* response));
  MOCK_METHOD1(onFailure, void(Http::AsyncClient::FailureReason reason));
};

class MockAsyncClientStreamCallbacks : public AsyncClient::StreamCallbacks {
public:
  MockAsyncClientStreamCallbacks();
  ~MockAsyncClientStreamCallbacks();

  void onHeaders(HeaderMapPtr&& headers, bool end_stream) override {
    onHeaders_(*headers, end_stream);
  }
  void onTrailers(HeaderMapPtr&& trailers) override { onTrailers_(*trailers); }

  MOCK_METHOD2(onHeaders_, void(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(onData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(onTrailers_, void(HeaderMap& headers));
  MOCK_METHOD0(onReset, void());
};

class MockAsyncClientRequest : public AsyncClient::Request {
public:
  MockAsyncClientRequest(MockAsyncClient* client);
  ~MockAsyncClientRequest();

  MOCK_METHOD0(cancel, void());

  MockAsyncClient* client_;
};

class MockAsyncClientStream : public AsyncClient::Stream {
public:
  MockAsyncClientStream();
  ~MockAsyncClientStream();

  MOCK_METHOD2(sendHeaders, void(HeaderMap& headers, bool end_stream));
  MOCK_METHOD2(sendData, void(Buffer::Instance& data, bool end_stream));
  MOCK_METHOD1(sendTrailers, void(HeaderMap& trailers));
  MOCK_METHOD0(reset, void());
};

class MockFilterChainFactoryCallbacks : public Http::FilterChainFactoryCallbacks {
public:
  MockFilterChainFactoryCallbacks();
  ~MockFilterChainFactoryCallbacks();

  MOCK_METHOD1(addStreamDecoderFilter, void(Http::StreamDecoderFilterSharedPtr filter));
  MOCK_METHOD1(addStreamEncoderFilter, void(Http::StreamEncoderFilterSharedPtr filter));
  MOCK_METHOD1(addStreamFilter, void(Http::StreamFilterSharedPtr filter));
  MOCK_METHOD1(addAccessLogHandler, void(AccessLog::InstanceSharedPtr handler));
};

class MockDownstreamWatermarkCallbacks : public DownstreamWatermarkCallbacks {
public:
  MOCK_METHOD0(onAboveWriteBufferHighWatermark, void());
  MOCK_METHOD0(onBelowWriteBufferLowWatermark, void());
};

} // namespace Http

namespace Http {
namespace ConnectionPool {

class MockCancellable : public Cancellable {
public:
  MockCancellable();
  ~MockCancellable();

  // Http::ConnectionPool::Cancellable
  MOCK_METHOD0(cancel, void());
};

class MockInstance : public Instance {
public:
  MockInstance();
  ~MockInstance();

  // Http::ConnectionPool::Instance
  MOCK_CONST_METHOD0(protocol, Http::Protocol());
  MOCK_METHOD1(addDrainedCallback, void(DrainedCb cb));
  MOCK_METHOD0(drainConnections, void());
  MOCK_METHOD2(newStream, Cancellable*(Http::StreamDecoder& response_decoder,
                                       Http::ConnectionPool::Callbacks& callbacks));

  std::shared_ptr<testing::NiceMock<Upstream::MockHostDescription>> host_{
      new testing::NiceMock<Upstream::MockHostDescription>()};
};

} // namespace ConnectionPool

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
template <typename T, typename K> HeaderValueOfMatcher HeaderValueOf(K key, T matcher) {
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

  IsSubsetOfHeadersMatcherImpl(IsSubsetOfHeadersMatcherImpl&& other)
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

  const HeaderMapImpl expected_headers_;
};

class IsSubsetOfHeadersMatcher {
public:
  IsSubsetOfHeadersMatcher(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  IsSubsetOfHeadersMatcher(IsSubsetOfHeadersMatcher&& other)
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  IsSubsetOfHeadersMatcher(const IsSubsetOfHeadersMatcher& other)
      : expected_headers_(static_cast<const HeaderMap&>(other.expected_headers_)) {}

  template <typename HeaderMapT> operator testing::Matcher<HeaderMapT>() const {
    return testing::MakeMatcher(new IsSubsetOfHeadersMatcherImpl<HeaderMapT>(expected_headers_));
  }

private:
  HeaderMapImpl expected_headers_;
};

IsSubsetOfHeadersMatcher IsSubsetOfHeaders(const HeaderMap& expected_headers);

} // namespace Http

MATCHER_P(HeaderMapEqual, rhs, "") {
  Http::HeaderMapImpl& lhs = *dynamic_cast<Http::HeaderMapImpl*>(arg.get());
  return lhs == *rhs;
}

MATCHER_P(HeaderMapEqualRef, rhs, "") {
  const Http::HeaderMapImpl& lhs = *dynamic_cast<const Http::HeaderMapImpl*>(&arg);
  return lhs == *dynamic_cast<const Http::HeaderMapImpl*>(rhs);
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
