#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/http/header_map_impl.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "contrib/checksum/filters/http/source/checksum_filter.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ChecksumFilter {
namespace {

class ChecksumFilterTest : public testing::Test {
public:
  void SetUp() override {
    envoy::extensions::filters::http::checksum::v3alpha::ChecksumConfig config;
    config.set_reject_unmatched(true);
    for (const absl::string_view path : {"/abc", "/123"}) {
      auto entry = config.add_checksums();
      entry->mutable_path_matcher()->set_exact(path);
      // The sha256 of the string "banana"
      entry->set_sha256("b493d48364afe44d11c0165cf470a4164d1e2609911ef998be868d46ade3de4e");
    }
    config_ = std::make_shared<ChecksumFilterConfig>(config);
    filter_ = std::make_unique<ChecksumFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  ~ChecksumFilterTest() override { filter_->onDestroy(); }

  std::shared_ptr<ChecksumFilterConfig> config_;
  std::unique_ptr<ChecksumFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
};

TEST_F(ChecksumFilterTest, RejectsUnmatched) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, "no_checksum_for_path"));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/1234"}};
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers, true));
}

TEST_F(ChecksumFilterTest, RejectsMatchedNoData) {
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, "checksum_but_no_body"));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, true));
}

TEST_F(ChecksumFilterTest, RejectsMatchedWithMismatchedData) {
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, "mismatched_checksum"));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl wrong_buffer{"aaa"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(wrong_buffer, true));
}

TEST_F(ChecksumFilterTest, RejectsMatchedWithMismatchedDataAndTrailers) {
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, "mismatched_checksum"));
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl wrong_buffer{"aaa"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(wrong_buffer, false));
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
}

TEST_F(ChecksumFilterTest, AcceptsMatchedWithMatchingData) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl right_buffer{"banana"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(right_buffer, true));
}

TEST_F(ChecksumFilterTest, AcceptsMatchedWithMatchingDataInParts) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl right_buffer_1{"ban"};
  Buffer::OwnedImpl right_buffer_2{"ana"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(right_buffer_1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(right_buffer_2, true));
}

TEST_F(ChecksumFilterTest, AcceptsMatchedWithMatchingDataAndTrailers) {
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  EXPECT_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).Times(0);
  Http::TestRequestHeaderMapImpl request_headers{{":path", "/123"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl right_buffer{"banana"};
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(right_buffer, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
}

} // namespace
} // namespace ChecksumFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
