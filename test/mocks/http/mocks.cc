#include "mocks.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::IsSubsetOf;
using testing::MakeMatcher;
using testing::Matcher;
using testing::MatcherInterface;
using testing::MatchResultListener;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Http {

MockConnectionCallbacks::MockConnectionCallbacks() {}
MockConnectionCallbacks::~MockConnectionCallbacks() {}

MockServerConnectionCallbacks::MockServerConnectionCallbacks() {}
MockServerConnectionCallbacks::~MockServerConnectionCallbacks() {}

MockStreamDecoder::MockStreamDecoder() {}
MockStreamDecoder::~MockStreamDecoder() {}

MockStreamCallbacks::MockStreamCallbacks() {}
MockStreamCallbacks::~MockStreamCallbacks() {}

MockStream::MockStream() {
  ON_CALL(*this, addCallbacks(_)).WillByDefault(Invoke([this](StreamCallbacks& callbacks) -> void {
    callbacks_.push_back(&callbacks);
  }));

  ON_CALL(*this, removeCallbacks(_))
      .WillByDefault(
          Invoke([this](StreamCallbacks& callbacks) -> void { callbacks_.remove(&callbacks); }));

  ON_CALL(*this, resetStream(_)).WillByDefault(Invoke([this](StreamResetReason reason) -> void {
    for (StreamCallbacks* callbacks : callbacks_) {
      callbacks->onResetStream(reason);
    }
  }));
}

MockStream::~MockStream() {}

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
}

MockStreamEncoder::~MockStreamEncoder() {}

MockServerConnection::MockServerConnection() {
  ON_CALL(*this, protocol()).WillByDefault(Return(protocol_));
}

MockServerConnection::~MockServerConnection() {}

MockClientConnection::MockClientConnection() {}
MockClientConnection::~MockClientConnection() {}

MockFilterChainFactory::MockFilterChainFactory() {}
MockFilterChainFactory::~MockFilterChainFactory() {}

template <class T> static void initializeMockStreamFilterCallbacks(T& callbacks) {
  callbacks.route_.reset(new NiceMock<Router::MockRoute>());
  ON_CALL(callbacks, dispatcher()).WillByDefault(ReturnRef(callbacks.dispatcher_));
  ON_CALL(callbacks, requestInfo()).WillByDefault(ReturnRef(callbacks.request_info_));
  ON_CALL(callbacks, route()).WillByDefault(Return(callbacks.route_));
}

MockStreamDecoderFilterCallbacks::MockStreamDecoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, decodingBuffer()).WillByDefault(Invoke(&buffer_, &Buffer::InstancePtr::get));

  ON_CALL(*this, addDownstreamWatermarkCallbacks(_))
      .WillByDefault(Invoke([this](DownstreamWatermarkCallbacks& callbacks) -> void {
        callbacks_.push_back(&callbacks);
      }));

  ON_CALL(*this, removeDownstreamWatermarkCallbacks(_))
      .WillByDefault(Invoke([this](DownstreamWatermarkCallbacks& callbacks) -> void {
        callbacks_.remove(&callbacks);
      }));

  ON_CALL(*this, activeSpan()).WillByDefault(ReturnRef(active_span_));
  ON_CALL(*this, tracingConfig()).WillByDefault(ReturnRef(tracing_config_));
}

MockStreamDecoderFilterCallbacks::~MockStreamDecoderFilterCallbacks() {}

MockStreamEncoderFilterCallbacks::MockStreamEncoderFilterCallbacks() {
  initializeMockStreamFilterCallbacks(*this);
  ON_CALL(*this, encodingBuffer()).WillByDefault(Invoke(&buffer_, &Buffer::InstancePtr::get));
  ON_CALL(*this, activeSpan()).WillByDefault(ReturnRef(active_span_));
  ON_CALL(*this, tracingConfig()).WillByDefault(ReturnRef(tracing_config_));
}

MockStreamEncoderFilterCallbacks::~MockStreamEncoderFilterCallbacks() {}

MockStreamDecoderFilter::MockStreamDecoderFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](StreamDecoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockStreamDecoderFilter::~MockStreamDecoderFilter() {}

MockStreamEncoderFilter::MockStreamEncoderFilter() {
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke(
          [this](StreamEncoderFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockStreamEncoderFilter::~MockStreamEncoderFilter() {}

MockStreamFilter::MockStreamFilter() {
  ON_CALL(*this, setDecoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamDecoderFilterCallbacks& callbacks) -> void {
        decoder_callbacks_ = &callbacks;
      }));
  ON_CALL(*this, setEncoderFilterCallbacks(_))
      .WillByDefault(Invoke([this](StreamEncoderFilterCallbacks& callbacks) -> void {
        encoder_callbacks_ = &callbacks;
      }));
}

MockStreamFilter::~MockStreamFilter() {}

MockAsyncClient::MockAsyncClient() {
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}
MockAsyncClient::~MockAsyncClient() {}

MockAsyncClientCallbacks::MockAsyncClientCallbacks() {}
MockAsyncClientCallbacks::~MockAsyncClientCallbacks() {}

MockAsyncClientStreamCallbacks::MockAsyncClientStreamCallbacks() {}
MockAsyncClientStreamCallbacks::~MockAsyncClientStreamCallbacks() {}

MockAsyncClientRequest::MockAsyncClientRequest(MockAsyncClient* client) : client_(client) {}
MockAsyncClientRequest::~MockAsyncClientRequest() { client_->onRequestDestroy(); }

MockAsyncClientStream::MockAsyncClientStream() {}
MockAsyncClientStream::~MockAsyncClientStream() {}

MockFilterChainFactoryCallbacks::MockFilterChainFactoryCallbacks() {}
MockFilterChainFactoryCallbacks::~MockFilterChainFactoryCallbacks() {}

} // namespace Http

namespace Http {
namespace ConnectionPool {

MockCancellable::MockCancellable() {}
MockCancellable::~MockCancellable() {}

MockInstance::MockInstance() {}
MockInstance::~MockInstance() {}

} // namespace ConnectionPool

bool HeaderValueOfMatcher::MatchAndExplain(const HeaderMap& headers,
                                           testing::MatchResultListener* listener) const {
  // Get all headers with matching keys.
  std::vector<absl::string_view> values;
  std::pair<std::string, std::vector<absl::string_view>*> context =
      std::make_pair(key_.get(), &values);
  Envoy::Http::HeaderMap::ConstIterateCb get_headers_cb = [](const Envoy::Http::HeaderEntry& header,
                                                             void* context) {
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

void HeaderValueOfMatcher::DescribeTo(std::ostream* os) const {
  *os << "has a '" << key_.get() << "' header with value that "
      << testing::DescribeMatcher<absl::string_view>(matcher_);
}

void HeaderValueOfMatcher::DescribeNegationTo(std::ostream* os) const {
  *os << "doesn't have a '" << key_.get() << "' header with value that "
      << testing::DescribeMatcher<absl::string_view>(matcher_);
}

namespace {

class IsSubsetOfHeadersMatcher : public MatcherInterface<const HeaderMap&> {
public:
  explicit IsSubsetOfHeadersMatcher(const HeaderMap& expected_headers)
      : expected_headers_(expected_headers) {}

  bool MatchAndExplain(const HeaderMap& headers, MatchResultListener* listener) const override {
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

    return ExplainMatchResult(IsSubsetOf(expected_headers_vec), arg_headers_vec, listener);
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "is a subset of headers:\n" << expected_headers_;
  }

  const HeaderMap& expected_headers_;
};

} // namespace

Matcher<const HeaderMap&> IsSubsetOfHeaders(const HeaderMap& expected_headers) {
  return MakeMatcher(new IsSubsetOfHeadersMatcher(expected_headers));
}

// Test that a HeaderMapPtr argument includes a given key-value pair, e.g.,
//  HeaderHasValue("Upgrade", "WebSocket")
testing::Matcher<const Http::HeaderMap*> HeaderHasValue(const std::string& key,
                                                        const std::string& value) {
  return testing::Pointee(Http::HeaderValueOf(key, value));
}

// Like HeaderHasValue, but matches against a (const) HeaderMap& argument.
testing::Matcher<const Http::HeaderMap&> HeaderHasValueRef(const std::string& key,
                                                           const std::string& value) {
  return Http::HeaderValueOf(key, value);
}

} // namespace Http
} // namespace Envoy
