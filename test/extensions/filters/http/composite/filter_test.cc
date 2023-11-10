#include <memory>

#include "envoy/http/metadata_interface.h"

#include "source/extensions/filters/http/composite/filter.h"

#include "test/mocks/access_log/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Composite {
namespace {

class FilterTest : public ::testing::Test {
public:
  FilterTest() : filter_(stats_, decoder_callbacks_.dispatcher()) {
    filter_.setDecoderFilterCallbacks(decoder_callbacks_);
    filter_.setEncoderFilterCallbacks(encoder_callbacks_);
  }

  // Templated since MockStreamFilter and MockStreamDecoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedDecoding(T& filter_mock) {
    EXPECT_CALL(filter_mock, decodeHeaders(HeaderMapEqualRef(&default_request_headers_), false));
    EXPECT_CALL(filter_mock, decodeMetadata(_));
    EXPECT_CALL(filter_mock, decodeData(_, false));
    EXPECT_CALL(filter_mock, decodeTrailers(HeaderMapEqualRef(&default_request_trailers_)));
    EXPECT_CALL(filter_mock, decodeComplete());
  }

  // Templated since MockStreamFilter and MockStreamEncoder filter doesn't share a mock base class.
  template <class T> void expectDelegatedEncoding(T& filter_mock) {
    EXPECT_CALL(filter_mock, encode1xxHeaders(HeaderMapEqualRef(&default_response_headers_)));
    EXPECT_CALL(filter_mock, encodeHeaders(HeaderMapEqualRef(&default_response_headers_), false));
    EXPECT_CALL(filter_mock, encodeMetadata(_));
    EXPECT_CALL(filter_mock, encodeData(_, false));
    EXPECT_CALL(filter_mock, encodeTrailers(HeaderMapEqualRef(&default_response_trailers_)));
    EXPECT_CALL(filter_mock, encodeComplete());
  }

  void doAllDecodingCallbacks() {
    filter_.decodeHeaders(default_request_headers_, false);

    Http::MetadataMap metadata;
    filter_.decodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.decodeData(buffer, false);

    filter_.decodeTrailers(default_request_trailers_);

    filter_.decodeComplete();
  }

  void doAllEncodingCallbacks() {
    filter_.encode1xxHeaders(default_response_headers_);

    filter_.encodeHeaders(default_response_headers_, false);

    Http::MetadataMap metadata;
    filter_.encodeMetadata(metadata);

    Buffer::OwnedImpl buffer("data");
    filter_.encodeData(buffer, false);

    filter_.encodeTrailers(default_response_trailers_);

    filter_.encodeComplete();
  }

  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
  Stats::MockCounter error_counter_;
  Stats::MockCounter success_counter_;
  FilterStats stats_{error_counter_, success_counter_};
  Filter filter_;

  Http::TestRequestHeaderMapImpl default_request_headers_{
      {":method", "GET"}, {":path", "/test/long/url"}, {":scheme", "http"}, {":authority", "host"}};
  Http::TestRequestTrailerMapImpl default_request_trailers_{{"trailers", "something"}};
  Http::TestResponseHeaderMapImpl default_response_headers_{{":status", "200"}};
  Http::TestResponseTrailerMapImpl default_response_trailers_{
      {"response-trailer", "something-else"}};
};

TEST_F(FilterTest, StreamEncoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamEncoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamDecoderFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamDecoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onStreamComplete());
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onStreamComplete();
  filter_.onDestroy();
}

TEST_F(FilterTest, StreamFilterDelegation) {
  auto stream_filter = std::make_shared<Http::MockStreamFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
  };

  EXPECT_CALL(*stream_filter, setDecoderFilterCallbacks(_));
  EXPECT_CALL(*stream_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());
  ExecuteFilterAction action(factory_callback);
  filter_.onMatchCallback(action);

  expectDelegatedDecoding(*stream_filter);
  doAllDecodingCallbacks();
  expectDelegatedEncoding(*stream_filter);
  doAllEncodingCallbacks();
  EXPECT_CALL(*stream_filter, onDestroy());
  filter_.onDestroy();
}

// Adding multiple stream filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamFilters) {
  auto stream_filter = std::make_shared<Http::MockStreamFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamFilter(stream_filter);
    cb.addStreamFilter(stream_filter);
  };

  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding multiple decoder filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamDecoderFilters) {
  auto decoder_filter = std::make_shared<Http::MockStreamDecoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamDecoderFilter(decoder_filter);
    cb.addStreamDecoderFilter(decoder_filter);
  };

  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding multiple encoder filters should cause delegation to be skipped.
TEST_F(FilterTest, StreamFilterDelegationMultipleStreamEncoderFilters) {
  auto encode_filter = std::make_shared<Http::MockStreamEncoderFilter>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addStreamEncoderFilter(encode_filter);
  };

  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(error_counter_, inc());
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  doAllEncodingCallbacks();
  filter_.onDestroy();
}

// Adding a encoder filter and an access loggers should be permitted and delegate to the access
// logger.
TEST_F(FilterTest, StreamFilterDelegationMultipleAccessLoggers) {
  auto encode_filter = std::make_shared<Http::MockStreamEncoderFilter>();
  auto access_log_1 = std::make_shared<AccessLog::MockInstance>();
  auto access_log_2 = std::make_shared<AccessLog::MockInstance>();

  auto factory_callback = [&](Http::FilterChainFactoryCallbacks& cb) {
    cb.addStreamEncoderFilter(encode_filter);
    cb.addAccessLogHandler(access_log_1);
    cb.addAccessLogHandler(access_log_2);
  };

  ExecuteFilterAction action(factory_callback);
  EXPECT_CALL(*encode_filter, setEncoderFilterCallbacks(_));
  EXPECT_CALL(success_counter_, inc());
  filter_.onMatchCallback(action);

  doAllDecodingCallbacks();
  expectDelegatedEncoding(*encode_filter);
  doAllEncodingCallbacks();

  EXPECT_CALL(*encode_filter, onDestroy());
  filter_.onDestroy();

  EXPECT_CALL(*access_log_1, log(_, _));
  EXPECT_CALL(*access_log_2, log(_, _));
  filter_.log({}, StreamInfo::MockStreamInfo());
}

} // namespace
} // namespace Composite
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
