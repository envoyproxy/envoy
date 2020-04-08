#include <memory>

#include "common/common/hex.h"
#include "common/decompressor/zlib_decompressor_impl.h"

#include "extensions/filters/http/compressor/compressor_filter.h"
#include "extensions/filters/http/compressor/gzip/config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

class GzipCompressorFilterTest : public testing::Test {
protected:
  void SetUp() override {
    envoy::extensions::filters::http::compressor::v3::Compressor compressor;
    envoy::extensions::filters::http::compressor::gzip::v3::Gzip gzip;
    CompressorFactoryPtr gzip_factory = std::make_unique<Gzip::GzipCompressorFactory>(gzip);
    config_ = std::make_shared<CompressorFilterConfig>(compressor, "test.", stats_, runtime_,
                                                       std::move(gzip_factory));
    filter_ = std::make_unique<Common::Compressors::CompressorFilter>(config_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    decompressor_.init(31);
  }

  // GzipCompressorFilterTest Helpers
  void verifyCompressedData(const uint32_t content_length) {
    // This makes sure we have a finished buffer before sending it to the client.
    expectValidFinishedBuffer(content_length);
    decompressor_.decompress(data_, decompressed_data_);
    const std::string uncompressed_str{decompressed_data_.toString()};
    ASSERT_EQ(expected_str_.length(), uncompressed_str.length());
    EXPECT_EQ(expected_str_, uncompressed_str);
    EXPECT_EQ(expected_str_.length(), stats_.counter("test.gzip.total_uncompressed_bytes").value());
    EXPECT_EQ(data_.length(), stats_.counter("test.gzip.total_compressed_bytes").value());
  }

  void feedBuffer(uint64_t size) {
    TestUtility::feedBufferWithRandomCharacters(data_, size);
    expected_str_ += data_.toString();
  }

  void drainBuffer() {
    const uint64_t data_len = data_.length();
    data_.drain(data_len);
  }

  void doRequest(Http::TestRequestHeaderMapImpl&& headers, bool end_stream) {
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, end_stream));
  }

  void doResponseCompression(Http::TestResponseHeaderMapImpl&& headers, bool with_trailers) {
    uint64_t content_length;
    ASSERT_TRUE(absl::SimpleAtoi(headers.get_("content-length"), &content_length));
    feedBuffer(content_length);
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
    EXPECT_EQ("", headers.get_("content-length"));
    EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip, headers.get_("content-encoding"));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, !with_trailers));
    if (with_trailers) {
      Buffer::OwnedImpl trailers_buffer;
      EXPECT_CALL(encoder_callbacks_, addEncodedData(_, true))
          .WillOnce(Invoke([&](Buffer::Instance& data, bool) { data_.move(data); }));
      Http::TestResponseTrailerMapImpl trailers;
      EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
    }
    verifyCompressedData(content_length);
    drainBuffer();
    EXPECT_EQ(1U, stats_.counter("test.gzip.compressed").value());
  }

  void expectValidFinishedBuffer(const uint32_t content_length) {
    Buffer::RawSliceVector compressed_slices = data_.getRawSlices();
    const uint64_t num_comp_slices = compressed_slices.size();

    const std::string header_hex_str = Hex::encode(
        reinterpret_cast<unsigned char*>(compressed_slices[0].mem_), compressed_slices[0].len_);
    // HEADER 0x1f = 31 (window_bits)
    EXPECT_EQ("1f8b", header_hex_str.substr(0, 4));
    // CM 0x8 = deflate (compression method)
    EXPECT_EQ("08", header_hex_str.substr(4, 2));

    const std::string footer_bytes_str =
        Hex::encode(reinterpret_cast<unsigned char*>(compressed_slices[num_comp_slices - 1].mem_),
                    compressed_slices[num_comp_slices - 1].len_);

    // A valid finished compressed buffer should have trailer with input size in it, i.e. equals to
    // the value of content_length.
    expectEqualInputSize(footer_bytes_str, content_length);
  }

  void expectEqualInputSize(const std::string& footer_bytes, const uint32_t input_size) {
    const std::string size_bytes = footer_bytes.substr(footer_bytes.size() - 8, 8);
    uint64_t size;
    StringUtil::atoull(size_bytes.c_str(), size, 16);
    EXPECT_EQ(TestUtility::flipOrder<uint32_t>(size), input_size);
  }

  void doResponseNoCompression(Http::TestResponseHeaderMapImpl&& headers) {
    uint64_t content_length;
    ASSERT_TRUE(absl::SimpleAtoi(headers.get_("content-length"), &content_length));
    feedBuffer(content_length);
    Http::TestResponseHeaderMapImpl continue_headers;
    EXPECT_EQ(Http::FilterHeadersStatus::Continue,
              filter_->encode100ContinueHeaders(continue_headers));
    Http::MetadataMap metadata_map{{"metadata", "metadata"}};
    EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata_map));
    EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
    EXPECT_EQ("", headers.get_("content-encoding"));
    EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
    Http::TestResponseTrailerMapImpl trailers;
    EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(trailers));
    EXPECT_EQ(1, stats_.counter("test.gzip.not_compressed").value());
  }

  std::shared_ptr<CompressorFilterConfig> config_;
  std::unique_ptr<Common::Compressors::CompressorFilter> filter_;
  Buffer::OwnedImpl data_;
  Decompressor::ZlibDecompressorImpl decompressor_;
  Buffer::OwnedImpl decompressed_data_;
  std::string expected_str_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
};

// Acceptance Testing with default configuration.
TEST_F(GzipCompressorFilterTest, AcceptanceGzipEncoding) {
  doRequest({{":method", "get"}, {"accept-encoding", "deflate, gzip"}}, false);
  Http::MetadataMap metadata_map{{"metadata", "metadata"}};
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->decodeMetadata(metadata_map));
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  doResponseCompression({{":method", "get"}, {"content-length", "256"}}, false);
}

TEST_F(GzipCompressorFilterTest, AcceptanceGzipEncodingWithTrailers) {
  doRequest({{":method", "get"}, {"accept-encoding", "deflate, gzip"}}, false);
  Buffer::OwnedImpl data("hello");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data, false));
  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(trailers));
  doResponseCompression({{":method", "get"}, {"content-length", "256"}}, true);
}

// Verifies that compression is skipped when accept-encoding header is not allowed.
TEST_F(GzipCompressorFilterTest, AcceptEncodingNoCompression) {
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=0, deflate"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Verifies that compression is NOT skipped when accept-encoding header is allowed.
TEST_F(GzipCompressorFilterTest, AcceptEncodingCompression) {
  doRequest({{":method", "get"}, {"accept-encoding", "gzip, deflate"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}}, false);
}

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
