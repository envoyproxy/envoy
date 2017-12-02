#include "common/decompressor/zlib_decompressor_impl.h"
#include "common/http/filter/gzip_filter.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class GzipFilterTest : public testing::Test {
public:
  GzipFilterTest() : filter_(nullptr) {}

  const std::string minimum_conf_json = R"EOF({})EOF";

  const std::string full_conf_json = R"EOF(
    {
      "memory_level" : 1,
      "compression_level": "best"
    }
  )EOF";

  void setUpTest(const std::string json) {
    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    config_.reset(new GzipFilterConfig(*config));
    filter_.reset(new GzipFilter(config_));
    decompressor_.init(31);
  }

  void verifyCompressedData() {
    decompressor_.decompress(data_, decompressed_data_);
    const std::string uncompressed_str{TestUtility::bufferToString(decompressed_data_)};
    ASSERT_EQ(expected_str_.length(), uncompressed_str.length());
    EXPECT_EQ(expected_str_, uncompressed_str);
  }

  void feedBuffer(uint64_t size) {
    TestUtility::feedBufferWithRandomCharacters(data_, size);
    expected_str_ += TestUtility::bufferToString(data_);
  }

  void drainBuffer() {
    const uint64_t data_len = data_.length();
    data_.drain(data_len);
  }

  GzipFilterConfigSharedPtr config_;
  Buffer::OwnedImpl data_;
  Decompressor::ZlibDecompressorImpl decompressor_;
  Buffer::OwnedImpl decompressed_data_;
  std::string expected_str_;
  std::unique_ptr<GzipFilter> filter_;
  TestHeaderMapImpl request_headers_;
};

/**
 * Exercises gzip filter by applying compression on data dispatched from the upstream.
 */
TEST_F(GzipFilterTest, AcceptanceGzipEncoding) {
  setUpTest(minimum_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "deflate, gzip"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"},
                                           {"content-type", "application/json"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip,
            response_headers.get_("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));

  verifyCompressedData();
}

/**
 * Exercises gzip filter by compression on chunked data dispatched from the upstream.
 */
TEST_F(GzipFilterTest, AcceptanceGzipEncodingDataStatus) {
  setUpTest(minimum_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "deflate, gzip"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"},
                                           {"content-type", "application/json"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip,
            response_headers.get_("content-encoding"));
  EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data_, false));

  drainBuffer();
  feedBuffer(768);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));

  verifyCompressedData();
}

/**
 * Exercises gzip filter when `accept-encoding` value is wildcard.
 */
TEST_F(GzipFilterTest, AcceptEncondingWildcardValue) {
  setUpTest(minimum_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "*"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"},
                                           {"content-type", "application/json"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip,
            response_headers.get_("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));

  verifyCompressedData();
}

/**
 * Exercises gzip filter when client has not specified `accept-encoding` header.
 */
TEST_F(GzipFilterTest, NoAcceptEncodingHeader) {
  setUpTest(minimum_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_FALSE(response_headers.has("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(expected_str_, TestUtility::bufferToString(data_));
}

/**
 * Exercises gzip filter when `content-type` whitelist is empty.
 */
TEST_F(GzipFilterTest, EmptyContentTypeConfig) {
  setUpTest(minimum_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "deflate, gzip"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"},
                                           {"content-type", "application/json"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip,
            response_headers.get_("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));

  verifyCompressedData();
}

/**
 * Exercises gzip filter when upstream responds with a not whitelisted `content-type`.
 */
TEST_F(GzipFilterTest, NotSupportedContentType) {
  setUpTest(full_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "deflate, gzip"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{{":method", "get"},
                                           {"content-type", "application/javascript"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_FALSE(response_headers.has("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(expected_str_, TestUtility::bufferToString(data_));
}

/**
 * Exercises gzip filter when upstream responds with already encoded data.
 */
TEST_F(GzipFilterTest, AlreadeContentEncoding) {
  setUpTest(full_conf_json);
  Http::TestHeaderMapImpl request_headers{{":method", "get"}, {"accept-encoding", "deflate, gzip"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, false));
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers, true));

  feedBuffer(1024);
  Http::TestHeaderMapImpl response_headers{
      {":method", "get"}, {"content-encoding", "gzip"}, {"content-type", "application/json"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, true));
  EXPECT_TRUE(response_headers.has("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
  EXPECT_EQ(expected_str_, TestUtility::bufferToString(data_));
}

} // namespace Http
} // namespace Envoy
