#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"

#include "common/protobuf/utility.h"
#include "common/http/headers.h"

#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "test/mocks/compression/decompressor/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::Return;
using testing::ByMove;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {
namespace {

class DecompressorFilterTest : public testing::Test {
public:
  DecompressorFilterTest() {}

  void SetUp() override {
    setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");
  }

  void setUpFilter(std::string&& yaml) {
    envoy::extensions::filters::http::decompressor::v3::Decompressor decompressor;
    TestUtility::loadFromYaml(yaml, decompressor);
    auto decompressor_factory = std::make_unique<NiceMock<Compression::Decompressor::MockDecompressorFactory>>();
    decompressor_factory_ = decompressor_factory.get();
    config_ = std::make_shared<DecompressorFilterConfig>(decompressor, "test.", stats_,
                                                             runtime_, std::move(decompressor_factory));
    filter_ = std::make_unique<DecompressorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  Compression::Decompressor::MockDecompressorFactory* decompressor_factory_{};
  DecompressorFilterConfigSharedPtr config_;
  std::unique_ptr<DecompressorFilter> filter_;
  std::string expected_str_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(DecompressorFilterTest, ResponseDecompressionActive) {
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).WillOnce(Return(ByMove(std::move(decompressor))));
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "mock"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ(nullptr, headers.ContentEncoding());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ("chunked", headers.TransferEncoding()->value().getStringView());

  EXPECT_CALL(*decompressor_ptr, decompress(_, _)).Times(2).WillRepeatedly(Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
    TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
  }));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  EXPECT_EQ(20, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(40, buffer.length());
}

TEST_F(DecompressorFilterTest, ResponseDecompressionActiveMultipleEncodings) {
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).WillOnce(Return(ByMove(std::move(decompressor))));
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "mock, br"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_EQ("br", headers.ContentEncoding()->value().getStringView());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers.ContentLength());
  EXPECT_EQ("chunked", headers.TransferEncoding()->value().getStringView());

  EXPECT_CALL(*decompressor_ptr, decompress(_, _)).Times(2).WillRepeatedly(Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
    TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
  }));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
  EXPECT_EQ(20, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(40, buffer.length());
}

TEST_F(DecompressorFilterTest, ResponseDecompressionDisabled) {
    setUpFilter(R"EOF(
response_decompression_enabled:
  default_value: false
  runtime_key: does_not_exist
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");

  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "mock"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(10, buffer.length());
}

TEST_F(DecompressorFilterTest, ResponseDecompressionContentEncodingDoesNotMatch) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "not-matching"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(10, buffer.length());
}

TEST_F(DecompressorFilterTest, ResponseDecompressionContentEncodingNotCurrent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestResponseHeaderMapImpl headers{{"content-encoding", "gzip,mock"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(10, buffer.length());
}

TEST_F(DecompressorFilterTest, ResponseDecompressionNoTransformPresent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestResponseHeaderMapImpl headers{{"cache-control", Http::Headers::get().CacheControlValues.NoTransform}, {"content-encoding", "mock"}, {"content-length", "256"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer;
  TestUtility::feedBufferWithRandomCharacters(buffer, 10);
  EXPECT_EQ(10, buffer.length());
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, true));
  EXPECT_EQ(10, buffer.length());
}

} // namespace
} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
