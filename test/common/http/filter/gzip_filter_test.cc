#include "common/compressor/zlib_compressor_impl.h"
#include "common/config/filter_json.h"
#include "common/decompressor/zlib_decompressor_impl.h"
#include "common/http/filter/gzip_filter.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Http {

class GzipFilterTest : public testing::Test {
public:
  GzipFilterTest() {}

  void SetUp() override { decompressor_.init(31); }

  void setUpTest(std::string&& json) {
    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    envoy::api::v2::filter::http::Gzip gzip;

    Config::FilterJson::translateGzipFilter(*config, gzip);
    config_.reset(new GzipFilterConfig(gzip));
    filter_.reset(new GzipFilter(config_));
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

  void doRequest(Http::TestHeaderMapImpl&& headers, bool end_stream) {
    EXPECT_EQ(FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, end_stream));
  }

  void doResponseCompression(Http::TestHeaderMapImpl&& headers) {
    uint64_t content_length;
    ASSERT_TRUE(StringUtil::atoul(headers.get_("content-length").c_str(), content_length));
    feedBuffer(content_length);
    EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
    EXPECT_EQ("", headers.get_("content-length"));
    EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip, headers.get_("content-encoding"));
    EXPECT_EQ(FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data_, false));
    drainBuffer();
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
    verifyCompressedData();
  }

  void doResponseNoCompression(Http::TestHeaderMapImpl&& headers) {
    uint64_t content_length;
    ASSERT_TRUE(StringUtil::atoul(headers.get_("content-length").c_str(), content_length));
    feedBuffer(content_length);
    EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
    EXPECT_EQ("", headers.get_("content-encoding"));
    EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
  }

  void gzipFilterBadConfigHelper(std::string&& json) {
    Json::ObjectSharedPtr config = Json::Factory::loadFromString(json);
    envoy::api::v2::filter::http::Gzip gzip;
    EXPECT_THROW(Config::FilterJson::translateGzipFilter(*config, gzip), EnvoyException);
  }

protected:
  GzipFilterConfigSharedPtr config_;
  std::unique_ptr<GzipFilter> filter_;
  Buffer::OwnedImpl data_;
  Decompressor::ZlibDecompressorImpl decompressor_;
  Buffer::OwnedImpl decompressed_data_;
  std::string expected_str_;
};

// Default config values.
TEST_F(GzipFilterTest, DefaultConfigValues) {
  setUpTest("{}");
  EXPECT_EQ(5, config_->memoryLevel());
  EXPECT_EQ(30, config_->minimumLength());
  EXPECT_EQ(28, config_->windowBits());
  EXPECT_EQ(false, config_->disableOnEtag());
  EXPECT_EQ(false, config_->disableOnLastModified());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
            config_->compressionStrategy());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
            config_->compressionLevel());
  EXPECT_EQ(8, config_->contentTypeValues().size());
}

// Bad configuration - memory_level is out of range.
TEST_F(GzipFilterTest, BadConfigMemoryLevelOutOfRange) {
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 10 })EOF");
}

// Bad configuration - memory_level is zero.
TEST_F(GzipFilterTest, BadConfigMemoryLevelZero) {
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 0 })EOF");
}

// Bad configuration - window_bits is out of range.
TEST_F(GzipFilterTest, BadConfigWindowBitsOutOfRange) {
  gzipFilterBadConfigHelper(R"EOF({ "window_bits" : 16 })EOF");
}

// Bad configuration - window_bits is out of range.
// This should failed when window bits bug in zlib gets fixed
// and gzip.proto gets updated to support window size 8.
TEST_F(GzipFilterTest, BadConfigWindowBitsBellowSupported) {
  gzipFilterBadConfigHelper(R"EOF({ "window_bits" : 8 })EOF");
}

// Bad configuration - content_length is zero.
TEST_F(GzipFilterTest, BadConfigContentLengthZero) {
  gzipFilterBadConfigHelper(R"EOF({ "content_length" : 0 })EOF");
}

// Bad configuration - compression_level has invalid value.
TEST_F(GzipFilterTest, BadConfigCompressionLevelInvalid) {
  gzipFilterBadConfigHelper(R"EOF({ "compression_level" : "banana" })EOF");
}

// Bad configuration - compression_strategy is invalid.
TEST_F(GzipFilterTest, BadConfigCompressionStrategyInvalid) {
  gzipFilterBadConfigHelper(R"EOF({ "compression_strategy" : "banana" })EOF");
}

// Bad configuration - disable_on_etag has invalid value.
TEST_F(GzipFilterTest, BadConfigDisableOnEtagInvalid) {
  gzipFilterBadConfigHelper(R"EOF({ "disable_on_etag" : "banana" })EOF");
}

// Bad configuration - disable_on_last_modified has invalid value.
TEST_F(GzipFilterTest, BadConfigDisableLastModifiedInvalid) {
  gzipFilterBadConfigHelper(R"EOF({ "disable_on_last_modified" : "banana" })EOF");
}

// Bad configuration - config has invalid key/val.
TEST_F(GzipFilterTest, BadConfigInvalidKey) {
  gzipFilterBadConfigHelper(R"EOF({ "banana" : "banana" })EOF");
}

// Bad configuration - content_type exceeded 30 items.
TEST_F(GzipFilterTest, BadConfigContentTypeExceededLimit) {
  gzipFilterBadConfigHelper(R"EOF(
    {
      "content_type" : [
        "val1", "val2", "val3", "val4", "val5",
        "val6", "val7", "val8", "val9", "val10",
        "val11", "val12", "val13", "val14", "val15",
        "val16", "val17", "val18", "val19", "val20",
        "val21", "val22", "val23", "val24", "val25",
        "val26", "val27", "val28", "val29", "val30",
        "val31", "val32", "val33", "val34", "val35",
        "val36", "val37", "val38", "val39", "val40",
        "val41", "val42", "val43", "val44", "val45",
        "val46", "val47", "val48", "val49", "val50",
        "val51"
      ]
    }
  )EOF");
}

// Acceptance Testing with default configuration.
TEST_F(GzipFilterTest, AcceptanceGzipEncoding) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "deflate, gzip"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip;q=0, deflate.
TEST_F(GzipFilterTest, AcceptEncodingValuesQ0) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=0, deflate"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip;q=.5, deflate.
TEST_F(GzipFilterTest, AcceptEncodingGzipQ5) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=.5, deflate"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip;q=.5, identity.
TEST_F(GzipFilterTest, AcceptEncodingGzipQ5Identity) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=.5, identity"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: deflate, gzip;q=0.5, identity;q=0.
TEST_F(GzipFilterTest, AcceptEncodingGzipIdentityQ0) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "deflate;q=1, gzip;q=0.8, identity;q=0"}},
            true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: *
TEST_F(GzipFilterTest, AcceptEncodingWildcard) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "*"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: identity.
TEST_F(GzipFilterTest, AcceptEncodingIdentity) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "identity"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Content-Length below default configuration.
TEST_F(GzipFilterTest, ContentLengthBellowDefault) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "10"}});
}

// Content-Length below 500.
TEST_F(GzipFilterTest, ContentLengthBellowSomeValue) {
  setUpTest(R"EOF({"content_length": 500})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Content-Type is not in the white-list.
TEST_F(GzipFilterTest, ContentTypeNotSupported) {
  setUpTest(R"EOF(
    {
      "content_type": [
        "text/html",
        "text/css",
        "text/plain",
        "application/javascript",
        "application/json",
        "font/eot",
        "image/svg+xml"
      ]
    }
  )EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"content-type", "image/jpeg"}});
}

// Content-Type contains paramater.
TEST_F(GzipFilterTest, ContentTypeWithParameter) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"},
                         {"content-length", "256"},
                         {"content-type", "application/json;charset=utf-8"}});
}

// Last-Modified disable true.
TEST_F(GzipFilterTest, LastModifiedDisableTrue) {
  setUpTest(R"EOF({ "disable_on_last_modified": true })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"},
                           {"content-length", "256"},
                           {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Last-Modified default configuration.
TEST_F(GzipFilterTest, LastModifiedDefault) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"},
                         {"content-length", "256"},
                         {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Etag disable true.
TEST_F(GzipFilterTest, EtagDisableTrue) {
  setUpTest(R"EOF({ "disable_on_etag": true })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", R"EOF(W/"686897696a7c876b7e")EOF"}});
}

// Weak Etag default configuration.
TEST_F(GzipFilterTest, EtagDefault) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", R"EOF(W/"686897696a7c876b7e")EOF"}});
}

// Strong Etag and default configuration.
TEST_F(GzipFilterTest, StrongEtag) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}});
}

// Transfer-Encoding chunked.
TEST_F(GzipFilterTest, TransferEncodingChunked) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked"}});
}

// Transfer-Encoding gzip.
TEST_F(GzipFilterTest, TransferEncodingGzip) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked, deflate"}});
}

// Content-Encoding: upstream response is already encoded.
TEST_F(GzipFilterTest, ContentEncodingAlreadyEncoded) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl response_headers{
      {":method", "get"}, {"content-length", "256"}, {"content-encoding", "deflate, gzip"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.has("content-length"));
  EXPECT_FALSE(response_headers.has("transfer-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
}

// No compression when upstream response is empty.
TEST_F(GzipFilterTest, EmptyResponse) {
  setUpTest("{}");
  Http::TestHeaderMapImpl headers{{":method", "get"}, {":status", "204"}};
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, true));
  EXPECT_EQ("", headers.get_("content-length"));
  EXPECT_EQ("", headers.get_("content-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, true));
}

// Filter should set Vary header value with `accept-encoding`.
TEST_F(GzipFilterTest, NoVaryHeader) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{{":method", "get"}, {"content-length", "256"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_TRUE(headers.has("vary"));
  EXPECT_EQ("accept-encoding", headers.get_("vary"));
}

// Filter should set Vary header value with `accept-encoding` and remove `*`.
TEST_F(GzipFilterTest, VaryRemoveWildcard) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{{":method", "get"}, {"content-length", "256"}, {"vary", "*, cookie"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_TRUE(headers.has("vary"));
  EXPECT_EQ("cookie, accept-encoding", headers.get_("vary"));
}

// Vary header should have only one `accept-encoding`value.
TEST_F(GzipFilterTest, VaryAlreadyHasAcceptEncoding) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{
      {":method", "get"}, {"content-length", "256"}, {"vary", "*, accept-encoding"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_TRUE(headers.has("vary"));
  EXPECT_EQ("accept-encoding", headers.get_("vary"));
}

} // namespace Http
} // namespace Envoy
