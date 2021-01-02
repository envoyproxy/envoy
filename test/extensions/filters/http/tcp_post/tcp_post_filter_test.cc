#include "envoy/extensions/filters/http/tcp_post/v3/tcp_post.pb.h"
#include "envoy/http/metadata_interface.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/http/tcp_post/tcp_post_filter.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TcpPost {
namespace {

using ::testing::Return;

class TcpPostFilterTest : public testing::Test {
protected:
  void setUpTest(const envoy::extensions::filters::http::tcp_post::v3::TcpPost& tcp_post) {
    filter_ = std::make_unique<TcpPostFilter>(tcp_post);
  }

  std::unique_ptr<TcpPostFilter> filter_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

TEST_F(TcpPostFilterTest, ConvertPostToConnect) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  auto* header_match = tcp_post.add_headers();
  header_match->set_name("test");
  header_match->set_exact_match("foo");

  setUpTest(tcp_post);
  request_headers_.addCopy("test", "foo");
  request_headers_.setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Post);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Envoy::Http::Headers::get().MethodValues.Connect, request_headers_.getMethodValue());
  EXPECT_EQ(Envoy::Http::Headers::get().ProtocolValues.Bytestream,
            request_headers_.getProtocolValue());
}

TEST_F(TcpPostFilterTest, NotConvert_NonePostRequest) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  auto* header_match = tcp_post.add_headers();
  header_match->set_name("test");
  header_match->set_exact_match("foo");

  setUpTest(tcp_post);
  request_headers_.addCopy("test", "foo");
  request_headers_.setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Get);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Envoy::Http::Headers::get().MethodValues.Get, request_headers_.getMethodValue());
  EXPECT_EQ("", request_headers_.getProtocolValue());
}

TEST_F(TcpPostFilterTest, NotConvert_HeadersNotMatch) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  auto* header_match = tcp_post.add_headers();
  header_match->set_name("test");
  header_match->set_exact_match("foo");

  setUpTest(tcp_post);
  request_headers_.addCopy("test", "bar");
  request_headers_.setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Post);

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Envoy::Http::Headers::get().MethodValues.Post, request_headers_.getMethodValue());
  EXPECT_EQ("", request_headers_.getProtocolValue());
}

TEST_F(TcpPostFilterTest, DecodeDataReturnsContinue) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  setUpTest(tcp_post);
  Buffer::OwnedImpl data;
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
}

TEST_F(TcpPostFilterTest, DecodeTrailersReturnsContinue) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  setUpTest(tcp_post);
  Http::TestRequestTrailerMapImpl request_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
}

TEST_F(TcpPostFilterTest, Encode100ContinueHeadersReturnsContinue) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  setUpTest(tcp_post);
  Http::TestResponseHeaderMapImpl response_headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue,
            filter_->encode100ContinueHeaders(response_headers));
}

TEST_F(TcpPostFilterTest, EncodeTrailersReturnsContinue) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  setUpTest(tcp_post);
  Http::TestResponseTrailerMapImpl response_trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
}

TEST_F(TcpPostFilterTest, EncodeMetadataReturnsContinue) {
  envoy::extensions::filters::http::tcp_post::v3::TcpPost tcp_post;
  setUpTest(tcp_post);
  Http::MetadataMap metadata_map;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
}

} // namespace
} // namespace TcpPost
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
