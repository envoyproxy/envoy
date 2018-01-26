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
  EXPECT_EQ(false, config_->disableOnEtagHeader());
  EXPECT_EQ(false, config_->disableOnLastModifiedHeader());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionStrategy::Standard,
            config_->compressionStrategy());
  EXPECT_EQ(Compressor::ZlibCompressorImpl::CompressionLevel::Standard,
            config_->compressionLevel());
  EXPECT_EQ(8, config_->contentTypeValues().size());
}

// Verifies bad config params.
TEST_F(GzipFilterTest, BadConfigurationParams) {
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 10 })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "memory_level" : 0 })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "window_bits" : 16 })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "window_bits" : 8 })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "content_length" : 0 })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "compression_level" : "banana" })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "compression_strategy" : "banana" })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "disable_on_etag" : "banana" })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "disable_on_last_modified" : "banana" })EOF");
  gzipFilterBadConfigHelper(R"EOF({ "banana" : "banana" })EOF");
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

// Verifies isAcceptEncodingAllowed function.
TEST_F(GzipFilterTest, isAcceptEncodingAllowed) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "deflate, gzip, br"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "deflate, gzip;q=1.0, *;q=0.5"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {
        {"accept-encoding", "\tdeflate\t, gzip\t ; q\t =\t 1.0,\t * ;q=0.5\n"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "deflate,gzip;q=1.0,*;q=0"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "deflate, gzip;q=0.2, br;q=1"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "*"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "*;q=1"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "gzip;q=0,*;q=1"}};
    EXPECT_FALSE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity, *;q=0"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity;q=0.5, *;q=0"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity;q=0, *;q=0"}};
    EXPECT_FALSE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "xyz;q=1, br;q=0.2, *"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "xyz;q=1, br;q=0.2, *;q=0"}};
    EXPECT_FALSE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "xyz;q=1, br;q=0.2"}};
    EXPECT_FALSE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity;q=1"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity;q=0"}};
    EXPECT_FALSE(filter_->isAcceptEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"accept-encoding", "identity, *;q=0"}};
    EXPECT_TRUE(filter_->isAcceptEncodingAllowed(headers));
  }
}

// Verifies that compression is skipped when accept-encoding header is not allowed.
TEST_F(GzipFilterTest, AcceptEncodingNoCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip;q=0, deflate"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "256"}});
}

// Verifies that compression is NOT skipped when accept-encoding header is allowed.
TEST_F(GzipFilterTest, AcceptEncodingCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip, deflate"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "256"}});
}

// Verifies isMinimumContentLength function.
TEST_F(GzipFilterTest, isMinimumContentLength) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {{"content-length", "31"}};
    EXPECT_TRUE(filter_->isMinimumContentLength(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-length", "29"}};
    EXPECT_FALSE(filter_->isMinimumContentLength(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
    EXPECT_TRUE(filter_->isMinimumContentLength(headers));
  }

  setUpTest(R"EOF({"content_length": 500})EOF");
  {
    TestHeaderMapImpl headers = {{"content-length", "501"}};
    EXPECT_TRUE(filter_->isMinimumContentLength(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
    EXPECT_TRUE(filter_->isMinimumContentLength(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-length", "499"}};
    EXPECT_FALSE(filter_->isMinimumContentLength(headers));
  }
}

// Verifies that compression is skipped when content-length header is NOT allowed.
TEST_F(GzipFilterTest, ContentLengthNoCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"}, {"content-length", "10"}});
}

// Verifies that compression is NOT skipped when content-length header is allowed.
TEST_F(GzipFilterTest, ContentLengthCompression) {
  setUpTest(R"EOF({"content_length": 500})EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"}, {"content-length", "1000"}});
}

// Verifies isContentTypeAllowed function.
TEST_F(GzipFilterTest, isContentTypeAllowed) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {{"content-type", "text/html"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "text/xml"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "text/plain"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "application/javascript"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "image/svg+xml"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "application/json;charset=utf-8"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "application/json"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "application/xhtml+xml"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "image/jpeg"}};
    EXPECT_FALSE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "\ttext/html\t\n"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }

  setUpTest(R"EOF(
    {
      "content_type": [
        "text/html",
        "xyz/svg+xml"
      ]
    }
  )EOF");
  {
    TestHeaderMapImpl headers = {{"content-type", "xyz/svg+xml"}};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "xyz/false"}};
    EXPECT_FALSE(filter_->isContentTypeAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"content-type", "image/jpeg"}};
    EXPECT_FALSE(filter_->isContentTypeAllowed(headers));
  }
}

// Verifies that compression is skipped when content-encoding header is NOT allowed.
TEST_F(GzipFilterTest, ContentTypeNoCompression) {
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

// Verifies that compression is NOT skipped when content-encoding header is allowed.
TEST_F(GzipFilterTest, ContentTypeCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"},
                         {"content-length", "256"},
                         {"content-type", "application/json;charset=utf-8"}});
}

// Verifies isLastModifiedAllowed function.
TEST_F(GzipFilterTest, isLastModifiedAllowed) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {{"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}};
    EXPECT_TRUE(filter_->isLastModifiedAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isLastModifiedAllowed(headers));
  }
  setUpTest(R"EOF({ "disable_on_last_modified_header": true })EOF");
  {
    TestHeaderMapImpl headers = {{"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}};
    EXPECT_FALSE(filter_->isLastModifiedAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isLastModifiedAllowed(headers));
  }
}

// Verifies that compression is skipped when content-encoding header is NOT allowed.
TEST_F(GzipFilterTest, LastModifiedNoCompression) {
  setUpTest(R"EOF({ "disable_on_last_modified_header": true })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression({{":method", "get"},
                           {"content-length", "256"},
                           {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Verifies that compression is NOT skipped when content-encoding header is allowed.
TEST_F(GzipFilterTest, LastModifiedCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression({{":method", "get"},
                         {"content-length", "256"},
                         {"last-modified", "Wed, 21 Oct 2015 07:28:00 GMT"}});
}

// Verifies isEtagAllowed function.
TEST_F(GzipFilterTest, isEtagAllowed) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {{"etag", R"EOF(W/"686897696a7c876b7e")EOF"}};
    EXPECT_TRUE(filter_->isEtagAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"etag", "686897696a7c876b7e"}};
    EXPECT_TRUE(filter_->isEtagAllowed(headers));
    EXPECT_FALSE(headers.has("etag"));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isEtagAllowed(headers));
    EXPECT_FALSE(headers.has("etag"));
  }

  setUpTest(R"EOF({ "disable_on_etag_header": true })EOF");
  {
    TestHeaderMapImpl headers = {{"etag", R"EOF(W/"686897696a7c876b7e")EOF"}};
    EXPECT_FALSE(filter_->isEtagAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"etag", "686897696a7c876b7e"}};
    EXPECT_FALSE(filter_->isEtagAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isEtagAllowed(headers));
  }
}

// Verifies that compression is skipped when etag header is NOT allowed.
TEST_F(GzipFilterTest, EtagNoCompression) {
  setUpTest(R"EOF({ "disable_on_etag_header": true })EOF");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseNoCompression(
      {{":method", "get"}, {"content-length", "256"}, {"etag", R"EOF(W/"686897696a7c876b7e")EOF"}});
}

// Verifies that compression is skipped when etag header is NOT allowed.
TEST_F(GzipFilterTest, EtagCompression) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{
      {":method", "get"}, {"content-length", "256"}, {"etag", "686897696a7c876b7e"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_FALSE(headers.has("etag"));
  EXPECT_EQ("gzip", headers.get_("content-encoding"));
}

// Verifies isTransferEncodingAllowed function.
TEST_F(GzipFilterTest, isTransferEncodingAllowed) {
  setUpTest("{}");
  {
    TestHeaderMapImpl headers = {};
    EXPECT_TRUE(filter_->isTransferEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "chunked"}};
    EXPECT_TRUE(filter_->isTransferEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "deflate"}};
    EXPECT_FALSE(filter_->isTransferEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "gzip"}};
    EXPECT_FALSE(filter_->isTransferEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", "gzip, chunked"}};
    EXPECT_FALSE(filter_->isTransferEncodingAllowed(headers));
  }
  {
    TestHeaderMapImpl headers = {{"transfer-encoding", " gzip\t,  chunked\t\n"}};
    EXPECT_FALSE(filter_->isTransferEncodingAllowed(headers));
  }
}

// Tests compression when Transfer-Encoding header exists.
TEST_F(GzipFilterTest, TransferEncodingChunked) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  doResponseCompression(
      {{":method", "get"}, {"content-length", "256"}, {"transfer-encoding", "chunked"}});
}

// Tests compression when Transfer-Encoding header exists.
TEST_F(GzipFilterTest, AcceptanceTransferEncodingGzip) {
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

// Filter should set Vary header value with `accept-encoding` and preserve other values.
TEST_F(GzipFilterTest, VaryOtherValues) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{
      {":method", "get"}, {"content-length", "256"}, {"vary", "user-agent, cookie"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_TRUE(headers.has("vary"));
  EXPECT_EQ("user-agent, cookie, accept-encoding", headers.get_("vary"));
}

// Vary header should have only one `accept-encoding`value.
TEST_F(GzipFilterTest, VaryAlreadyHasAcceptEncoding) {
  setUpTest("{}");
  doRequest({{":method", "get"}, {"accept-encoding", "gzip"}}, true);
  TestHeaderMapImpl headers{
      {":method", "get"}, {"content-length", "256"}, {"vary", "accept-encoding"}};
  feedBuffer(256);
  EXPECT_EQ(FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));
  EXPECT_TRUE(headers.has("vary"));
  EXPECT_EQ("accept-encoding", headers.get_("vary"));
}

} // namespace Http
} // namespace Envoy
