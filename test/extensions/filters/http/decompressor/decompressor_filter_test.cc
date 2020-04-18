#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"

#include "common/http/headers.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/decompressor/decompressor_filter.h"

#include "test/mocks/buffer/mocks.h"
#include "test/mocks/compression/decompressor/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ByMove;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {
namespace {

class DecompressorFilterTest : public testing::TestWithParam<bool> {
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
    auto decompressor_factory =
        std::make_unique<NiceMock<Compression::Decompressor::MockDecompressorFactory>>();
    decompressor_factory_ = decompressor_factory.get();
    config_ = std::make_shared<DecompressorFilterConfig>(decompressor, "test.", stats_, runtime_,
                                                         std::move(decompressor_factory));
    filter_ = std::make_unique<DecompressorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  bool requestDirection() { return GetParam(); }

  std::unique_ptr<Http::RequestOrResponseHeaderMap> doHeaders(const Http::HeaderMap& rhs,
                                                              const bool end_stream = false) {
    if (requestDirection()) {
      auto request_headers = Http::createHeaderMap<Http::TestRequestHeaderMapImpl>(rhs);
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->decodeHeaders(*request_headers, end_stream));
      return std::move(request_headers);
    } else {
      auto response_headers = Http::createHeaderMap<Http::TestResponseHeaderMapImpl>(rhs);
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->encodeHeaders(*response_headers, end_stream));
      return std::move(response_headers);
    }
  }

  void doData(Buffer::Instance& buffer, const bool end_stream = false) {
    if (requestDirection()) {
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, end_stream));
    } else {
      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, end_stream));
    }
  }

  void expectDecompression(Compression::Decompressor::MockDecompressor* decompressor_ptr) {
    EXPECT_CALL(*decompressor_ptr, decompress(_, _))
        .Times(2)
        .WillRepeatedly(
            Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
              TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
            }));
    Buffer::OwnedImpl buffer;
    TestUtility::feedBufferWithRandomCharacters(buffer, 10);
    EXPECT_EQ(10, buffer.length());
    doData(buffer);
    EXPECT_EQ(20, buffer.length());
    doData(buffer, true);
    EXPECT_EQ(40, buffer.length());
  }

  void expectNoDecompression() {
    Buffer::OwnedImpl buffer;
    TestUtility::feedBufferWithRandomCharacters(buffer, 10);
    EXPECT_EQ(10, buffer.length());
    doData(buffer);
    EXPECT_EQ(10, buffer.length());
  }

  Compression::Decompressor::MockDecompressorFactory* decompressor_factory_{};
  DecompressorFilterConfigSharedPtr config_;
  std::unique_ptr<DecompressorFilter> filter_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

INSTANTIATE_TEST_SUITE_P(RequestOrResponse, DecompressorFilterTest, ::testing::Values(true, false));

TEST_P(DecompressorFilterTest, DecompressionActive) {
  // Keep the decompressor to set expectations about it
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor())
      .WillOnce(Return(ByMove(std::move(decompressor))));
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);

  EXPECT_EQ(nullptr, headers_after_filter->ContentEncoding());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers_after_filter->ContentLength());
  EXPECT_EQ("chunked", headers_after_filter->TransferEncoding()->value().getStringView());

  expectDecompression(decompressor_ptr);
}

TEST_P(DecompressorFilterTest, DecompressionActiveMultipleEncodings) {
  // Keep the decompressor to set expectations about it
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor())
      .WillOnce(Return(ByMove(std::move(decompressor))));
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "mock, br"},
                                                {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);

  EXPECT_EQ("br", headers_after_filter->ContentEncoding()->value().getStringView());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers_after_filter->ContentLength());
  EXPECT_EQ("chunked", headers_after_filter->TransferEncoding()->value().getStringView());

  expectDecompression(decompressor_ptr);
}

TEST_P(DecompressorFilterTest, DecompressionActiveTransferEncodingPresentAlready) {
  // Keep the decompressor to set expectations about it
  auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
  auto* decompressor_ptr = decompressor.get();
  EXPECT_CALL(*decompressor_factory_, createDecompressor())
      .WillOnce(Return(ByMove(std::move(decompressor))));
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "mock, br"},
                                                {"content-length", "256"},
                                                {"transfer-encoding", "chunked"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);

  EXPECT_EQ("br", headers_after_filter->ContentEncoding()->value().getStringView());

  // FIX ME(junr03): pending decision on this.
  EXPECT_EQ(nullptr, headers_after_filter->ContentLength());
  EXPECT_EQ("chunked", headers_after_filter->TransferEncoding()->value().getStringView());

  expectDecompression(decompressor_ptr);
}

TEST_P(DecompressorFilterTest, DecompressionDisabled) {
  setUpFilter(R"EOF(
response_decompression_enabled:
  default_value: false
  runtime_key: does_not_exist
request_decompression_enabled:
  default_value: false
  runtime_key: does_not_exist
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
)EOF");

  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);
  TestUtility::headerMapEqualIgnoreOrder(headers_before_filter, *headers_after_filter);

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoDecompressionHeadersOnly) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestHeaderMapImpl headers_before_filter;
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, true);
  TestUtility::headerMapEqualIgnoreOrder(headers_before_filter, *headers_after_filter);
}

TEST_P(DecompressorFilterTest, NoDecompressionContentEncodingDoesNotMatch) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "not-matching"},
                                                {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);
  TestUtility::headerMapEqualIgnoreOrder(headers_before_filter, *headers_after_filter);

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoDecompressionContentEncodingNotCurrent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestHeaderMapImpl headers_before_filter{{"content-encoding", "gzip,mock"},
                                                {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);
  TestUtility::headerMapEqualIgnoreOrder(headers_before_filter, *headers_after_filter);

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoResponseDecompressionNoTransformPresent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor()).Times(0);
  Http::TestHeaderMapImpl headers_before_filter{
      {"cache-control", Http::Headers::get().CacheControlValues.NoTransform},
      {"content-encoding", "mock"},
      {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter);
  TestUtility::headerMapEqualIgnoreOrder(headers_before_filter, *headers_after_filter);

  expectNoDecompression();
}

} // namespace
} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
