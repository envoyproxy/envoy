#include "source/extensions/http/header_validators/envoy_default/error_codes.h"
#include "source/extensions/http/header_validators/envoy_default/path_normalizer.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class PathNormalizerTest : public testing::Test {
protected:
  PathNormalizerPtr create(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);
    return std::make_unique<PathNormalizer>(typed_config);
  }

  static constexpr absl::string_view empty_config = "{}";
  static constexpr absl::string_view impl_specific_slash_handling_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: IMPLEMENTATION_SPECIFIC_DEFAULT
    )EOF";
  static constexpr absl::string_view keep_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: KEEP_UNCHANGED
    )EOF";
  static constexpr absl::string_view reject_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: REJECT_REQUEST
    )EOF";
  static constexpr absl::string_view redirect_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_REDIRECT
    )EOF";
  static constexpr absl::string_view decode_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_FORWARD
    )EOF";
  static constexpr absl::string_view skip_merging_slashes_config = R"EOF(
    uri_path_normalization_options:
      skip_merging_slashes: true
    )EOF";
  static constexpr absl::string_view skip_merging_slashes_with_decode_slashes_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_FORWARD
      skip_merging_slashes: true
    )EOF";
};

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetDecoded) {
  std::string valid = "x%7ex";

  auto normalizer = create(empty_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(std::next(valid.begin()), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Decoded);
  EXPECT_EQ(decoded.octet(), '~');
  EXPECT_EQ(valid, "x%7Ex");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetNormalized) {
  std::string valid = "%ffX";

  auto normalizer = create(empty_config);

  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end()).result(),
            PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_EQ(valid, "%FFX");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetInvalid) {
  std::string invalid_length = "%";
  std::string invalid_length_2 = "%a";
  std::string invalid_hex = "%ax";

  auto normalizer = create(empty_config);

  EXPECT_EQ(
      normalizer->normalizeAndDecodeOctet(invalid_length.begin(), invalid_length.end()).result(),
      PathNormalizer::PercentDecodeResult::Invalid);
  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(invalid_length_2.begin(), invalid_length_2.end())
                .result(),
            PathNormalizer::PercentDecodeResult::Invalid);
  EXPECT_EQ(normalizer->normalizeAndDecodeOctet(invalid_hex.begin(), invalid_hex.end()).result(),
            PathNormalizer::PercentDecodeResult::Invalid);
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepNotSet) {
  std::string valid = "%2fx";

  auto normalizer = create(empty_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_EQ(valid, "%2Fx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepImplDefault) {
  std::string valid = "%2fx";

  auto normalizer = create(impl_specific_slash_handling_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_EQ(valid, "%2Fx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetKeepPathSepUnchanged) {
  std::string valid = "%2fx";

  auto normalizer = create(keep_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Normalized);
  EXPECT_EQ(valid, "%2Fx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetRejectEncodedSlash) {
  std::string valid = "%2fx";

  auto normalizer = create(reject_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Reject);
  EXPECT_EQ(valid, "%2Fx");
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetRedirectEncodedSlash) {
  std::string valid = "%2fx";

  auto normalizer = create(redirect_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::DecodedRedirect);
  EXPECT_EQ(valid, "%2Fx");
  EXPECT_EQ(decoded.octet(), '/');
}

TEST_F(PathNormalizerTest, NormalizeAndDecodeOctetDecodedEncodedSlash) {
  std::string valid = "%2fx";

  auto normalizer = create(decode_encoded_slash_config);
  auto decoded = normalizer->normalizeAndDecodeOctet(valid.begin(), valid.end());

  EXPECT_EQ(decoded.result(), PathNormalizer::PercentDecodeResult::Decoded);
  EXPECT_EQ(valid, "%2Fx");
  EXPECT_EQ(decoded.octet(), '/');
}

TEST_F(PathNormalizerTest, NormalizePathUriRoot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriRootPreserveQueryFragment) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/root/child?x=1#anchor"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/root/child?x=1#anchor");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriRootPreserveFragment) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/root/child#anchor"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/root/child#anchor");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDotDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/../dir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir2");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/./dir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/dir2");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriTrailingDotDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/.."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriEncodedDotDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/%2e./dir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir2");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriTrailingDot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDotInSegments) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1/.dir2/..dir3/dir.4/dir..5"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/.dir2/..dir3/dir.4/dir..5");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriMergeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "///root///child//"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/root/child/");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriPercentDecodeNormalized) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%ff"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/%FF");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriPercentDecoded) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%7e/dir1"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/~/dir1");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriSkipMergingSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "//root//child//"}};

  auto normalizer = create(skip_merging_slashes_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "//root//child//");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriSkipMergingSlashesWithDecodeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/root%2f/child/%2f"}};

  auto normalizer = create(skip_merging_slashes_with_decode_slashes_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/root//child//");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriDecodeSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2%2f/dir3"}};

  auto normalizer = create(decode_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(headers.path(), "/dir1/dir2/dir3");
  EXPECT_TRUE(result.ok());
}

TEST_F(PathNormalizerTest, NormalizePathUriRejectEncodedSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(reject_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriRedirectEncodedSlashes) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(redirect_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Redirect);
  EXPECT_EQ(result.details(), "uhv.path_noramlization_redirect");
  EXPECT_EQ(headers.path(), "/dir1/dir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesKeep) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(keep_encoded_slash_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriNormalizeEncodedSlashesImplDefault) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/dir1%2fdir2"}};

  auto normalizer = create(impl_specific_slash_handling_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(headers.path(), "/dir1%2Fdir2");
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidBeyondRoot) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/.."}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidRelative) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "./"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriInvalidEncoding) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/%x"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriAuthorityFormConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":path", ""}, {":authority", "envoy.com"}, {":method", "CONNECT"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Accept);
  EXPECT_EQ(headers.path(), "");
}

TEST_F(PathNormalizerTest, NormalizePathUriAuthorityFormWithPathConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":path", "/"}, {":authority", "envoy.com"}, {":method", "CONNECT"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriAuthorityFormNotConnect) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":path", ""}, {":authority", "envoy.com"}, {":method", "GET"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(PathNormalizerTest, NormalizePathUriAsteriskFormOptions) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":path", "*"}, {":authority", "envoy.com"}, {":method", "OPTIONS"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Accept);
  EXPECT_EQ(headers.path(), "*");
}

TEST_F(PathNormalizerTest, NormalizePathUriAsteriskFormNotOptions) {
  ::Envoy::Http::TestRequestHeaderMapImpl headers{
      {":path", "*"}, {":authority", "envoy.com"}, {":method", "GET"}};

  auto normalizer = create(empty_config);
  auto result = normalizer->normalizePathUri(headers);

  EXPECT_EQ(result.action(), PathNormalizer::PathNormalizationResult::Action::Reject);
  EXPECT_EQ(result.details(), UhvResponseCodeDetail::get().InvalidUrl);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
