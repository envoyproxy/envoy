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
  setUpTest(R"EOF({})EOF");
  EXPECT_EQ(8, config_->memoryLevel());
  EXPECT_EQ(30, config_->minimumLength());
  EXPECT_EQ(false, config_->disableOnEtag());
  EXPECT_EQ(false, config_->disableOnLastModified());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
            config_->compressionStrategy());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
            config_->compressionLevel());
  EXPECT_EQ(0, config_->cacheControlValues().size());
  EXPECT_EQ(0, config_->contentTypeValues().size());
}

// Bad configuration - memory_level is out of range.
TEST_F(GzipFilterTest, BadConfigMemoryLevelOutOfRange) {
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 10 })EOF");
}

// Bad configuration - memory_level is zero.
TEST_F(GzipFilterTest, BadConfigMemoryLevelZero) {
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 0 })EOF");
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

// Bad configuration - cache_control is not unique.
TEST_F(GzipFilterTest, BadConfigCacheControlNotUnique) {
  gzipFilterBadConfigHelper(R"EOF({ "cache_control" : ["val1", "val1"] })EOF");
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

// Bad configuration - cache_control exceeded 10 items.
TEST_F(GzipFilterTest, BadConfigCacheControlExceededLimit) {
  gzipFilterBadConfigHelper(R"EOF(
    {
      "cache_control" : [
        "val1", "val2", "val3", "val4", "val5",
        "val6", "val7", "val8", "val9", "val10",
        "val11"
      ]
    }
  )EOF");
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
        "val31"
      ]
    }
  )EOF");
}

// Acceptance Testing with default configuration.
TEST_F(GzipFilterTest, AcceptanceGzipEncoding) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "deflate, gzip"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip;q=0, identity;q=0.5, *;q=0.
TEST_F(GzipFilterTest, AcceptEncodingValues) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=0, identity;q=0.5, *;q=0"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: identity;q=0.5, gzip; q=0, *;q=0.
TEST_F(GzipFilterTest, AcceptEncodingGzipQ0) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "identity;q=0.5, gzip; q=0, *;q=0"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip;q=0, identity.
TEST_F(GzipFilterTest, AcceptEncodingGzipQ0NoSpace) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip; q=0, identity"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: identity;q=0.5, gzip, *;q=0.
TEST_F(GzipFilterTest, AcceptEncodingGzipNoQ0) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "identity;q=0.5, gzip, *;q=0"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: identity;q=0.5, gzip; q=0.8, br;q=0.
TEST_F(GzipFilterTest, AcceptEncodingGzipNoQ08) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "identity;q=0.5, gzip; q=0.8, br;q=0"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: *.
TEST_F(GzipFilterTest, AcceptEncodingGzipWildcard) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "*"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Accept-Encoding: gzip.
TEST_F(GzipFilterTest, AcceptEncodingGzipGzip) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Content-Length below default configuration.
TEST_F(GzipFilterTest, ContentLengthBellowDefault) {
  setUpTest(R"EOF({})EOF");
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

// Content-Type allows all types.
TEST_F(GzipFilterTest, ContentTypeAllowAll) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"content-type", "image/png"}});
}

// Cache-Control is not in the white-list.
TEST_F(GzipFilterTest, CacheControlNotAllowedValue) {
  setUpTest(R"EOF( {"cache_control": [ "no-cache", "no-store", "private" ] })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"cache-control", "max-age=1234"}});
}

// Cache-Control is not specified.
TEST_F(GzipFilterTest, CacheControlAllowAll) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"cache-control", "max-age=1234"}});
}

// Last-Modified disable true.
TEST_F(GzipFilterTest, LastModifiedDisableTrue) {
  setUpTest(R"EOF({ "disable_on_last_modified": true })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"},
                           {"content-length", "256"},
                           {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Last-Modified disable false.
TEST_F(GzipFilterTest, LastModifiedDisableFalse) {
  setUpTest(R"EOF({ "disable_on_last_modified": false })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"},
                         {"content-length", "256"},
                         {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Last-Modified default configuration.
TEST_F(GzipFilterTest, LastModifiedDefault) {
  setUpTest(R"EOF({})EOF");
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
      {{":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}});
}

// Etag disable false.
TEST_F(GzipFilterTest, EtagDisableFalse) {
  setUpTest(R"EOF({ "disable_on_etag": false })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}});
}

// Etag default configuration.
TEST_F(GzipFilterTest, EtagDefault) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}});
}

// Transfer-Encoding chunked.
TEST_F(GzipFilterTest, TransferEncodingChunked) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked"}});
}

// Transfer-Encoding gzip.
TEST_F(GzipFilterTest, TransferEncodingGzip) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "gzip"}});
}

// Content-Encoding: upstream response is already encoded.
TEST_F(GzipFilterTest, ContentEncodingAlreadyEncoded) {
  setUpTest(R"EOF({})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl response_headers{
      {":method", "get"}, {"content-length", "256"}, {"content-encoding", "deflate, gzip"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers, false));
  EXPECT_TRUE(response_headers.has("content-length"));
  EXPECT_FALSE(response_headers.has("transfer-encoding"));
  EXPECT_EQ(FilterDataStatus::Continue, filter_->encodeData(data_, false));
}

} // namespace Http
} // namespace Envoy
