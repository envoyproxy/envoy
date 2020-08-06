#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "library/common/extensions/filters/http/assertion/filter.h"
#include "library/common/extensions/filters/http/assertion/filter.pb.h"

using testing::ByMove;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Assertion {
namespace {

class AssertionFilterTest : public testing::Test {
public:
  void setUpFilter(std::string&& yaml) {
    envoymobile::extensions::filters::http::assertion::Assertion config;
    TestUtility::loadFromYaml(yaml, config);
    config_ = std::make_shared<AssertionFilterConfig>(config);
    filter_ = std::make_unique<AssertionFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  AssertionFilterConfigSharedPtr config_{};
  std::unique_ptr<AssertionFilter> filter_{};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(AssertionFilterTest, HeadersMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config:
  http_request_headers_match:
    headers:
      - name: ":authority"
        exact_match: test.code
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::OK, "Request Headers match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, HeadersMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_headers_match:
    headers:
      - name: ":authority"
        exact_match: test.code
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "test.code"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
}

TEST_F(AssertionFilterTest, HeadersNoMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_headers_match:
    headers:
      - name: ":authority"
        exact_match: test.code
)EOF");

  Http::TestRequestHeaderMapImpl request_headers{{":authority", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Headers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(AssertionFilterTest, DataMatchWithEndStream) {
  setUpFilter(R"EOF(
match_config:
  http_request_generic_body_match:
    patterns:
      - string_match: match_me
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::OK, "Request Body match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(*body, true));
}

TEST_F(AssertionFilterTest, DataMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_generic_body_match:
    patterns:
      - string_match: match_me
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("match_me")};

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(*body, false));
}

TEST_F(AssertionFilterTest, DataNoMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_generic_body_match:
    patterns:
      - string_match: match_me
)EOF");

  Buffer::InstancePtr body{new Buffer::OwnedImpl("garbage")};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Body does not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(*body, true));
}

TEST_F(AssertionFilterTest, TrailersMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_trailers_match:
    headers:
      - name: "test-trailer"
        exact_match: test.code
)EOF");

  Http::TestRequestTrailerMapImpl request_trailers{{"test-trailer", "test.code"}};

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::OK, "Request Trailers match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
}

TEST_F(AssertionFilterTest, TrailersNoMatch) {
  setUpFilter(R"EOF(
match_config:
  http_request_trailers_match:
    headers:
      - name: "test-trailer"
        exact_match: test.code
)EOF");

  Http::TestRequestTrailerMapImpl request_trailers{{"test-trailer", "no.match"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "Request Trailers do not match configured expectations", _, _, ""));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers));
}

} // namespace
} // namespace Assertion
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
