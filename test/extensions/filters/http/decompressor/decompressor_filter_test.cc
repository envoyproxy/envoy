#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"

#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/decompressor/decompressor_filter.h"

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
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {
namespace {

class DecompressorFilterTest : public testing::TestWithParam<bool> {
public:
  void SetUp() override {
    setUpFilter(R"EOF(
decompressor_library:
  name: testlib
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
    config_ = std::make_shared<DecompressorFilterConfig>(decompressor, "test.", *stats_.rootScope(),
                                                         runtime_, std::move(decompressor_factory));
    filter_ = std::make_unique<DecompressorFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  bool isRequestDirection() { return GetParam(); }

  std::unique_ptr<Http::RequestOrResponseHeaderMap> doHeaders(const Http::HeaderMap& headers,
                                                              const bool end_stream) {
    if (isRequestDirection()) {
      auto request_headers = std::make_unique<Http::TestRequestHeaderMapImpl>(headers);
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->decodeHeaders(*request_headers, end_stream));
      return request_headers;
    } else {
      auto response_headers = std::make_unique<Http::TestResponseHeaderMapImpl>(headers);
      EXPECT_EQ(Http::FilterHeadersStatus::Continue,
                filter_->encodeHeaders(*response_headers, end_stream));
      return response_headers;
    }
  }

  void doData(Buffer::Instance& buffer, const bool end_stream, const bool expect_decompression) {
    if (isRequestDirection()) {
      Http::TestRequestTrailerMapImpl trailers;
      if (end_stream && expect_decompression) {
        EXPECT_CALL(decoder_callbacks_, addDecodedTrailers()).WillOnce(ReturnRef(trailers));
      }

      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, end_stream));

      if (end_stream && expect_decompression) {
        EXPECT_EQ(
            "30",
            trailers.get(Http::LowerCaseString("x-envoy-decompressor-testlib-compressed-bytes"))[0]
                ->value()
                .getStringView());
        EXPECT_EQ("60", trailers
                            .get(Http::LowerCaseString(
                                "x-envoy-decompressor-testlib-uncompressed-bytes"))[0]
                            ->value()
                            .getStringView());
      }
    } else {
      Http::TestResponseTrailerMapImpl trailers;
      if (end_stream && expect_decompression) {
        EXPECT_CALL(encoder_callbacks_, addEncodedTrailers()).WillOnce(ReturnRef(trailers));
      }

      EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, end_stream));

      if (end_stream && expect_decompression) {
        EXPECT_EQ(
            "30",
            trailers.get(Http::LowerCaseString("x-envoy-decompressor-testlib-compressed-bytes"))[0]
                ->value()
                .getStringView());
        EXPECT_EQ("60", trailers
                            .get(Http::LowerCaseString(
                                "x-envoy-decompressor-testlib-uncompressed-bytes"))[0]
                            ->value()
                            .getStringView());
      }
    }
  }

  void doTrailers() {
    if (isRequestDirection()) {
      Http::TestRequestTrailerMapImpl request_trailers;
      EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers));
      EXPECT_EQ("30",
                request_trailers
                    .get(Http::LowerCaseString("x-envoy-decompressor-testlib-compressed-bytes"))[0]
                    ->value()
                    .getStringView());
      EXPECT_EQ("60", request_trailers
                          .get(Http::LowerCaseString(
                              "x-envoy-decompressor-testlib-uncompressed-bytes"))[0]
                          ->value()
                          .getStringView());
    } else {
      Http::TestResponseTrailerMapImpl response_trailers;
      EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers));
      EXPECT_EQ("30",
                response_trailers
                    .get(Http::LowerCaseString("x-envoy-decompressor-testlib-compressed-bytes"))[0]
                    ->value()
                    .getStringView());
      EXPECT_EQ("60", response_trailers
                          .get(Http::LowerCaseString(
                              "x-envoy-decompressor-testlib-uncompressed-bytes"))[0]
                          ->value()
                          .getStringView());
    }
  }

  void expectDecompression(Compression::Decompressor::MockDecompressor* decompressor_ptr,
                           bool end_with_data) {
    EXPECT_CALL(*decompressor_ptr, decompress(_, _))
        .Times(2)
        .WillRepeatedly(
            Invoke([&](const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) {
              TestUtility::feedBufferWithRandomCharacters(output_buffer, 2 * input_buffer.length());
            }));
    Buffer::OwnedImpl buffer;
    TestUtility::feedBufferWithRandomCharacters(buffer, 10);
    EXPECT_EQ(10, buffer.length());
    doData(buffer, false /* end_stream */, true /* expect_decompression */);
    EXPECT_EQ(20, buffer.length());
    doData(buffer, end_with_data /* end_stream */, true /* expect_decompression */);
    EXPECT_EQ(40, buffer.length());
    if (!end_with_data) {
      doTrailers();
    }
  }

  void expectNoDecompression() {
    Buffer::OwnedImpl buffer;
    TestUtility::feedBufferWithRandomCharacters(buffer, 10);
    EXPECT_EQ(10, buffer.length());
    doData(buffer, true /* end_stream */, false /* expect_decompression */);
    EXPECT_EQ(10, buffer.length());
  }

  void decompressionActive(const Http::HeaderMap& headers_before_filter, bool end_with_data,
                           const absl::optional<std::string> expected_content_encoding,
                           const absl::optional<std::string> expected_accept_encoding = "mock") {
    // Keep the decompressor to set expectations about it
    auto decompressor = std::make_unique<Compression::Decompressor::MockDecompressor>();
    auto* decompressor_ptr = decompressor.get();
    EXPECT_CALL(*decompressor_factory_, createDecompressor(_))
        .WillOnce(Return(ByMove(std::move(decompressor))));

    std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
        doHeaders(headers_before_filter, false /* end_stream */);

    // The filter removes Content-Length
    EXPECT_EQ(nullptr, headers_after_filter->ContentLength());

    // The filter removes the decompressor's content encoding from the Content-Encoding header.
    if (expected_content_encoding.has_value()) {
      EXPECT_EQ(expected_content_encoding.value(),
                headers_after_filter->get(Http::CustomHeaders::get().ContentEncoding)[0]
                    ->value()
                    .getStringView());
    } else {
      EXPECT_TRUE(headers_after_filter->get(Http::CustomHeaders::get().ContentEncoding).empty());
    }

    // The filter adds the decompressor's content encoding to the Accept-Encoding header on the
    // request direction.
    const auto accept_encoding =
        headers_after_filter->get(Http::LowerCaseString{"accept-encoding"});
    if (isRequestDirection() && expected_accept_encoding.has_value()) {
      EXPECT_EQ(expected_accept_encoding.value(), accept_encoding[0]->value().getStringView());
    } else {
      EXPECT_TRUE(accept_encoding.empty());
    }

    expectDecompression(decompressor_ptr, end_with_data);
  }

  void testAcceptEncodingFilter(const std::string& original_accept_encoding,
                                const std::string& final_accept_encoding) {
    setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
request_direction_config:
  advertise_accept_encoding: true
)EOF");
    Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                         {"content-length", "256"}};
    if (isRequestDirection()) {
      headers_before_filter.addCopy("accept-encoding", original_accept_encoding);
    }
    decompressionActive(headers_before_filter, true /* end_with_data */, absl::nullopt,
                        final_accept_encoding);
  }

  Compression::Decompressor::MockDecompressorFactory* decompressor_factory_{};
  DecompressorFilterConfigSharedPtr config_;
  std::unique_ptr<DecompressorFilter> filter_;
  Stats::TestUtil::TestStore stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

INSTANTIATE_TEST_SUITE_P(IsRequestDirection, DecompressorFilterTest,
                         ::testing::Values(true, false));

TEST_P(DecompressorFilterTest, DecompressionActive) {
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */,
                      absl::nullopt /* expected_content_encoding */);
}

TEST_P(DecompressorFilterTest, DecompressionActiveEndWithTrailers) {
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, false /* end_with_data */,
                      absl::nullopt /* expected_content_encoding */);
}

TEST_P(DecompressorFilterTest, DecompressionActiveContentEncodingSpacing) {
  // Additional spacing should still match.
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", " mock "},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */,
                      absl::nullopt /* expected_content_encoding */);
}

TEST_P(DecompressorFilterTest, DecompressionActiveContentEncodingCasing) {
  // Different casing should still match.
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "MOCK"},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */,
                      absl::nullopt /* expected_content_encoding */);
}

TEST_P(DecompressorFilterTest, DecompressionActiveMultipleEncodings) {
  // If the first encoding in the Content-Encoding header is the configured value, the filter should
  // still be active.
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock, br"},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */, "br");
}

TEST_P(DecompressorFilterTest, DecompressionActiveMultipleEncodings2) {
  // If the first encoding in the Content-Encoding header is the configured value, the filter should
  // still be active.
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock, br , gzip "},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */, "br , gzip");
}

TEST_P(DecompressorFilterTest, DisableAdvertiseAcceptEncoding) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
request_direction_config:
  advertise_accept_encoding: false
)EOF");

  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};
  decompressionActive(headers_before_filter, true /* end_with_data */,
                      absl::nullopt /* expected_content_encoding*/,
                      absl::nullopt /* expected_accept_encoding */);
}

TEST_P(DecompressorFilterTest, ExplicitlyEnableAdvertiseAcceptEncoding) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
request_direction_config:
  advertise_accept_encoding: true
)EOF");

  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};
  if (isRequestDirection()) {
    // Also test that the filter appends to an already existing header.
    headers_before_filter.addCopy("accept-encoding", "br");
  }
  decompressionActive(headers_before_filter, true /* end_with_data */,
                      absl::nullopt /* expected_content_encoding*/,
                      "br,mock" /* expected_accept_encoding */);
}

TEST_P(DecompressorFilterTest, ExplicitlyEnableAdvertiseAcceptEncodingOnlyOnce) {
  // Do not duplicate accept-encoding values. Remove extra accept-encoding values for the
  // content-type we specify. Also remove q-values from our content-type (if not set, it defaults
  // to 1.0). Test also whitespace in accept-encoding value string.
  testAcceptEncodingFilter("br,mock, mock\t,mock ;q=0.3", "br,mock");
}

TEST_P(DecompressorFilterTest, ExplicitlyEnableAdvertiseAcceptEncodingRemoveQValue) {
  // If the accept-encoding header had a q-value, it needs to be removed.
  testAcceptEncodingFilter("mock;q=0.6", "mock");
}

TEST_P(DecompressorFilterTest, DecompressionDisabled) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
request_direction_config:
  common_config:
    enabled:
      default_value: false
      runtime_key: does_not_exist
response_direction_config:
  common_config:
    enabled:
      default_value: false
      runtime_key: does_not_exist
)EOF");

  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, RequestDecompressionDisabled) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
request_direction_config:
  common_config:
    enabled:
      default_value: false
      runtime_key: does_not_exist
)EOF");

  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};

  if (isRequestDirection()) {
    EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
    std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
        doHeaders(headers_before_filter, false /* end_stream */);

    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
    EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

    expectNoDecompression();
  } else {
    decompressionActive(headers_before_filter, true /* end_with_data */,
                        absl::nullopt /* expected_content_encoding*/,
                        "mock" /* expected_accept_encoding */);
  }
}

TEST_P(DecompressorFilterTest, ResponseDecompressionDisabled) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
response_direction_config:
  common_config:
    enabled:
      default_value: false
      runtime_key: does_not_exist
)EOF");

  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock"},
                                                       {"content-length", "256"}};

  if (isRequestDirection()) {
    // Accept-Encoding is not advertised in the request headers when response decompression is
    // disabled.
    decompressionActive(headers_before_filter, true /* end_with_data */,
                        absl::nullopt /* expected_content_encoding*/,
                        absl::nullopt /* expected_accept_encoding */);
  } else {
    EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
    std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
        doHeaders(headers_before_filter, false /* end_stream */);

    EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

    expectNoDecompression();
  }
}

TEST_P(DecompressorFilterTest, DecompressionDisabledWhenNoTransformIsSet) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "mock, br , gzip "},
                                                       {"content-length", "256"},
                                                       {"cache-control", "no-transform"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest,
       DecompressionEnabledWhenNoTransformAndIgnoreNoTransformHeaderAreSet) {
  setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
response_direction_config:
  common_config:
    ignore_no_transform_header: true
)EOF");

  Http::TestRequestHeaderMapImpl headers_before_filter{
      {"content-encoding", "mock"}, {"content-length", "256"}, {"cache-control", "no-transform"}};
  if (isRequestDirection()) {
    EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
    std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
        doHeaders(headers_before_filter, false /* end_stream */);

    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
    EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

    expectNoDecompression();
  } else {
    decompressionActive(headers_before_filter, true /* end_with_data */,
                        absl::nullopt /* expected_content_encoding*/,
                        "mock" /* expected_accept_encoding */);
  }
}

TEST_P(DecompressorFilterTest, NoDecompressionHeadersOnly) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter;
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, true /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default, even for header-only requests.
    // Other than this header, the rest of the headers should be the same before and after the
    // filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));
}

TEST_P(DecompressorFilterTest, NoDecompressionContentEncodingAbsent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoDecompressionContentEncodingDoesNotMatch) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "not-matching"},
                                                       {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoDecompressionContentEncodingNotCurrent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  // The decompressor's content scheme is not the first value in the comma-delimited list in the
  // Content-Encoding header. Therefore, compression will not occur.
  Http::TestRequestHeaderMapImpl headers_before_filter{{"content-encoding", "gzip,mock"},
                                                       {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoResponseDecompressionNoTransformPresent) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{
      {"cache-control", Http::CustomHeaders::get().CacheControlValues.NoTransform},
      {"content-encoding", "mock"},
      {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, NoResponseDecompressionNoTransformPresentInList) {
  EXPECT_CALL(*decompressor_factory_, createDecompressor(_)).Times(0);
  Http::TestRequestHeaderMapImpl headers_before_filter{
      {"cache-control", fmt::format("{}, {}", Http::CustomHeaders::get().CacheControlValues.NoCache,
                                    Http::CustomHeaders::get().CacheControlValues.NoTransform)},
      {"content-encoding", "mock"},
      {"content-length", "256"}};
  std::unique_ptr<Http::RequestOrResponseHeaderMap> headers_after_filter =
      doHeaders(headers_before_filter, false /* end_stream */);

  if (isRequestDirection()) {
    ASSERT_EQ(headers_after_filter->get(Http::LowerCaseString("accept-encoding"))[0]
                  ->value()
                  .getStringView(),
              "mock");
    // The request direction adds Accept-Encoding by default. Other than this header, the rest of
    // the headers should be the same before and after the filter.
    headers_after_filter->remove(Http::LowerCaseString("accept-encoding"));
  }
  EXPECT_THAT(headers_after_filter, HeaderMapEqualIgnoreOrder(&headers_before_filter));

  expectNoDecompression();
}

TEST_P(DecompressorFilterTest, DecompressionLibraryNotRegistered) {
  EXPECT_THROW(setUpFilter(R"EOF(
decompressor_library:
  typed_config:
    "@type": "type.googleapis.com/envoy.extensions.compression.does_not_exist"
)EOF"),
               EnvoyException);
}

} // namespace
} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
