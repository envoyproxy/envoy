#include <filesystem>

#include "source/extensions/common/aws/utility.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::NiceMock;
using testing::Pair;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {
namespace {

MATCHER_P(WithName, expectedName, "") {
  *result_listener << "\nexpected { name: \"" << expectedName << "\"} but got {name: \""
                   << arg.name() << "\"}\n";
  return ExplainMatchResult(expectedName, arg.name(), result_listener);
}

const char CREDENTIALS_FILE_CONTENTS[] =
    R"(
[default]
aws_access_key_id=default_access_key
aws_secret_access_key=default_secret
aws_session_token=default_token

# This profile has leading spaces that should get trimmed.
  [profile1]
# The "=" in the value should not interfere with how this line is parsed.
aws_access_key_id=profile1_acc=ess_key
aws_secret_access_key=profile1_secret
foo=bar
aws_session_token=profile1_token

[profile2]
aws_access_key_id=profile2_access_key

[profile3]
aws_access_key_id=profile3_access_key
aws_secret_access_key=

[profile4]
aws_access_key_id = profile4_access_key
aws_secret_access_key = profile4_secret
aws_session_token = profile4_token
)";

TEST(UtilityTest, TestProfileResolver) {

  absl::flat_hash_map<std::string, std::string> elements = {{"AWS_ACCESS_KEY_ID", "testoverwrite"},
                                                            {"AWS_SECRET_ACCESS_KEY", ""}};
  absl::flat_hash_map<std::string, std::string>::iterator it;

  auto temp = TestEnvironment::temporaryDirectory();
  std::filesystem::create_directory(temp + "/.aws");
  std::string credential_file(temp + "/.aws/credentials");

  auto file_path = TestEnvironment::writeStringToFileForTest(
      credential_file, CREDENTIALS_FILE_CONTENTS, true, false);

  Utility::resolveProfileElements(file_path, "default", elements);
  it = elements.find("AWS_ACCESS_KEY_ID");
  EXPECT_EQ(it->second, "default_access_key");
  Utility::resolveProfileElements(file_path, "profile4", elements);
  it = elements.find("AWS_ACCESS_KEY_ID");
  EXPECT_EQ(it->second, "profile4_access_key");
}

// Headers must be in alphabetical order by virtue of std::map
TEST(UtilityTest, CanonicalizeHeadersInAlphabeticalOrder) {
  Http::TestRequestHeaderMapImpl headers{
      {"d", "d_value"}, {"f", "f_value"}, {"b", "b_value"},
      {"e", "e_value"}, {"c", "c_value"}, {"a", "a_value"},
  };
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map, ElementsAre(Pair("a", "a_value"), Pair("b", "b_value"), Pair("c", "c_value"),
                               Pair("d", "d_value"), Pair("e", "e_value"), Pair("f", "f_value")));
}

// HTTP pseudo-headers should be ignored
TEST(UtilityTest, CanonicalizeHeadersSkippingPseudoHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":path", "path_value"},
      {":method", "GET"},
      {"normal", "normal_value"},
  };
  std::vector<Envoy::Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map, ElementsAre(Pair("normal", "normal_value")));
}

// Repeated headers are joined with commas
TEST(UtilityTest, CanonicalizeHeadersJoiningDuplicatesWithCommas) {
  Http::TestRequestHeaderMapImpl headers{
      {"a", "a_value1"},
      {"a", "a_value2"},
      {"a", "a_value3"},
  };
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map, ElementsAre(Pair("a", "a_value1,a_value2,a_value3")));
}

// We canonicalize the :authority header as host
TEST(UtilityTest, CanonicalizeHeadersAuthorityToHost) {
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "authority_value"},
  };
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map, ElementsAre(Pair("host", "authority_value")));
}

// Ports 80 and 443 are omitted from the host headers
TEST(UtilityTest, CanonicalizeHeadersRemovingDefaultPortsFromHost) {
  Http::TestRequestHeaderMapImpl headers_port80{
      {":authority", "example.com:80"},
  };
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map_port80 = Utility::canonicalizeHeaders(headers_port80, exclusion_list);
  EXPECT_THAT(map_port80, ElementsAre(Pair("host", "example.com")));

  Http::TestRequestHeaderMapImpl headers_port443{
      {":authority", "example.com:443"},
  };
  const auto map_port443 = Utility::canonicalizeHeaders(headers_port443, exclusion_list);
  EXPECT_THAT(map_port443, ElementsAre(Pair("host", "example.com")));
}

// Whitespace is trimmed from headers
TEST(UtilityTest, CanonicalizeHeadersTrimmingWhitespace) {
  Http::TestRequestHeaderMapImpl headers{
      {"leading", "    leading value"},
      {"trailing", "trailing value    "},
      {"internal", "internal    value"},
      {"all", "    all    value    "},
  };
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map,
              ElementsAre(Pair("all", "all value"), Pair("internal", "internal value"),
                          Pair("leading", "leading value"), Pair("trailing", "trailing value")));
}

// Headers in the exclusion list are not canonicalized
TEST(UtilityTest, CanonicalizeHeadersDropExcludedMatchers) {
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Http::TestRequestHeaderMapImpl headers{
      {":authority", "example.com"},          {"x-forwarded-for", "1.2.3.4"},
      {"x-forwarded-proto", "https"},         {"x-amz-date", "20130708T220855Z"},
      {"x-amz-content-sha256", "e3b0c44..."}, {"x-envoy-retry-on", "5xx,reset"},
      {"x-envoy-max-retries", "3"},           {"x-amzn-trace-id", "0123456789"}};
  std::vector<Matchers::StringMatcherPtr> exclusion_list = {};
  std::vector<std::string> exact_matches = {"x-amzn-trace-id", "x-forwarded-for",
                                            "x-forwarded-proto"};
  for (auto& str : exact_matches) {
    envoy::type::matcher::v3::StringMatcher config;
    config.set_exact(str);
    exclusion_list.emplace_back(
        std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
            config, context));
  }
  std::vector<std::string> prefixes = {"x-envoy"};
  for (auto& match_str : prefixes) {
    envoy::type::matcher::v3::StringMatcher config;
    config.set_prefix(match_str);
    exclusion_list.emplace_back(
        std::make_unique<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>>(
            config, context));
  }
  const auto map = Utility::canonicalizeHeaders(headers, exclusion_list);
  EXPECT_THAT(map,
              ElementsAre(Pair("host", "example.com"), Pair("x-amz-content-sha256", "e3b0c44..."),
                          Pair("x-amz-date", "20130708T220855Z")));
}

// Verify the format of a minimalist canonical request
TEST(UtilityTest, MinimalCanonicalRequest) {
  std::map<std::string, std::string> headers;
  const auto request = Utility::createCanonicalRequest(
      "GET", "", headers, "content-hash", Utility::shouldNormalizeUriPath("vpc-lattice-svcs"),
      Utility::useDoubleUriEncode("vpc-lattice-svcs"));
  EXPECT_EQ(R"(GET
/



content-hash)",
            request);
}

TEST(UtilityTest, CanonicalRequestWithQueryString) {
  const std::map<std::string, std::string> headers;
  const auto request = Utility::createCanonicalRequest(
      "GET", "?query", headers, "content-hash", Utility::shouldNormalizeUriPath("vpc-lattice-svcs"),
      Utility::useDoubleUriEncode("vpc-lattice-svcs"));
  EXPECT_EQ(R"(GET
/
query=


content-hash)",
            request);
}

TEST(UtilityTest, CanonicalRequestWithHeaders) {
  const std::map<std::string, std::string> headers = {
      {"header1", "value1"},
      {"header2", "value2"},
      {"header3", "value3"},
  };
  const auto request = Utility::createCanonicalRequest(
      "GET", "", headers, "content-hash", Utility::shouldNormalizeUriPath("vpc-lattice-svcs"),
      Utility::useDoubleUriEncode("vpc-lattice-svcs"));
  EXPECT_EQ(R"(GET
/

header1:value1
header2:value2
header3:value3

header1;header2;header3
content-hash)",
            request);
}

TEST(UtilityTest, normalizePathReturnSlash) {
  const absl::string_view path = "";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, normalizePathSlash) {
  const absl::string_view path = "/";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, normalizePathDotDotMultiple) {
  const absl::string_view path = "/../../";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, normalizePathSlashes) {
  const absl::string_view path = "///";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/", canonical_path);
}

TEST(UtilityTest, normalizePathPrefixSlash) {
  const absl::string_view path = "test";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/test", canonical_path);
}

TEST(UtilityTest, normalizePathSuffixSlash) {
  const absl::string_view path = "test/";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/test/", canonical_path);
}

TEST(UtilityTest, normalizePathNormalizeSlash) {
  const absl::string_view path = "test////test///";
  const auto canonical_path = Utility::normalizePath(path);
  EXPECT_EQ("/test/test/", canonical_path);
}

TEST(UtilityTest, normalizePathWithEncoding) {
  const absl::string_view path = "test$file.txt";
  auto canonical_path = Utility::normalizePath(path);
  canonical_path = Utility::uriEncodePath(canonical_path);
  EXPECT_EQ("/test%24file.txt", canonical_path);
}

TEST(UtilityTest, normalizePathWithEncodingSpaces) {
  const absl::string_view path = "/test and test/";
  auto canonical_path = Utility::normalizePath(path);
  canonical_path = Utility::uriEncodePath(canonical_path);
  EXPECT_EQ("/test%20and%20test/", canonical_path);
}

TEST(UtilityTest, normalizePathWithAlreadyEncodedSpaces) {
  const absl::string_view path = "/test%20and%20test/";
  auto canonical_path = Utility::normalizePath(path);
  canonical_path = Utility::uriEncodePath(canonical_path);
  EXPECT_EQ("/test%2520and%2520test/", canonical_path);
}

TEST(UtilityTest, uriEncodePath) {
  const absl::string_view path = "test^!@=-_~.";
  const auto encoded_path = Utility::uriEncodePath(path);
  EXPECT_EQ("test%5E%21%40%3D-_~.", encoded_path);
}

TEST(UtilityTest, CanonicalizeQueryString) {
  const absl::string_view query = "a=1&b=2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&b=2", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringTrailingEquals) {
  const absl::string_view query = "a&b";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=&b=", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringSorted) {
  const absl::string_view query = "a=3&b=1&a=2&a=1";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1&a=2&a=3&b=1", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringEncoded) {
  const absl::string_view query = "a=^!@&b=/-_~.";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=%5E%21%40&b=%2F-_~.", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringWithPlus) {
  const absl::string_view query = "a=1+2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1%202", canonical_query);
}

TEST(UtilityTest, CanonicalizeQueryStringWithTilde) {
  const absl::string_view query = "a=1%7E~2";
  const auto canonical_query = Utility::canonicalizeQueryString(query);
  EXPECT_EQ("a=1~~2", canonical_query);
}

TEST(UtilityTest, EncodeQuerySegment) {
  const absl::string_view query = "^!@/-_~.";
  const auto encoded_query = Utility::encodeQueryComponent(query);
  EXPECT_EQ("%5E%21%40%2F-_~.", encoded_query);
}

TEST(UtilityTest, EncodeQuerySegmentReserved) {
  const absl::string_view query = "?=&";
  const auto encoded_query = Utility::encodeQueryComponent(query);
  EXPECT_EQ("%3F%3D%26", encoded_query);
}

TEST(UtilityTest, CanonicalizationFuzzTest) {
  std::string fuzz;
  fuzz.reserve(3);
  // Printable ASCII 32 - 126
  for (unsigned char i = 32; i <= 126; i++) {
    fuzz.push_back(i);
    for (unsigned char j = 32; j <= 126; j++) {
      fuzz.push_back(j);
      for (unsigned char k = 32; k <= 126; k++) {
        fuzz.push_back(k);
        Utility::uriEncodePath(fuzz);
        Utility::normalizePath(fuzz);
        Utility::encodeQueryComponent(fuzz);
        Utility::canonicalizeQueryString(fuzz);
        fuzz.pop_back();
      }
      fuzz.pop_back();
    }
    fuzz.pop_back();
  }
}

// Verify headers are joined with ";"
TEST(UtilityTest, JoinCanonicalHeaderNames) {
  std::map<std::string, std::string> headers = {
      {"header1", "value1"},
      {"header2", "value2"},
      {"header3", "value3"},
  };
  const auto names = Utility::joinCanonicalHeaderNames(headers);
  EXPECT_EQ("header1;header2;header3", names);
}

// Verify we return "" when there are no headers
TEST(UtilityTest, JoinCanonicalHeaderNamesWithEmptyMap) {
  std::map<std::string, std::string> headers;
  const auto names = Utility::joinCanonicalHeaderNames(headers);
  EXPECT_EQ("", names);
}

// Verify that createInternalCluster returns correctly
TEST(UtilityTest, CreateStaticClusterSuccess) {
  auto cluster = Utility::createInternalClusterStatic(
      "cluster_name", envoy::config::cluster::v3::Cluster::STATIC, "127.0.0.1:443");
  EXPECT_EQ(cluster.load_assignment()
                .endpoints(0)
                .lb_endpoints(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.1");
  EXPECT_EQ(cluster.load_assignment()
                .endpoints(0)
                .lb_endpoints(0)
                .endpoint()
                .address()
                .socket_address()
                .port_value(),
            443);
}

// Verify that for uri argument in addInternalClusterStatic port value is optional
// and can contain request path which will be ignored.
TEST(UtilityTest, CreateStaticClusterSuccessEvenWithMissingPort) {
  auto cluster = Utility::createInternalClusterStatic(
      "cluster_name", envoy::config::cluster::v3::Cluster::STATIC, "127.0.0.1/something");
  EXPECT_EQ(cluster.load_assignment()
                .endpoints(0)
                .lb_endpoints(0)
                .endpoint()
                .address()
                .socket_address()
                .address(),
            "127.0.0.1");
  EXPECT_EQ(cluster.load_assignment()
                .endpoints(0)
                .lb_endpoints(0)
                .endpoint()
                .address()
                .socket_address()
                .port_value(),
            80);
}

// The region is simply interpolated into sts.{}.amazonaws.com for most regions
// https://docs.aws.amazon.com/general/latest/gr/rande.html#sts_region.
TEST(UtilityTest, GetNormalAndFipsSTSEndpoints) {
  EXPECT_EQ("sts.ap-south-1.amazonaws.com", Utility::getSTSEndpoint("ap-south-1"));
  EXPECT_EQ("sts.some-new-region.amazonaws.com", Utility::getSTSEndpoint("some-new-region"));
#ifdef ENVOY_SSL_FIPS
  // Under FIPS mode the Envoy should fetch the credentials from FIPS the dedicated endpoints.
  EXPECT_EQ("sts-fips.us-east-1.amazonaws.com", Utility::getSTSEndpoint("us-east-1"));
  EXPECT_EQ("sts-fips.us-east-2.amazonaws.com", Utility::getSTSEndpoint("us-east-2"));
  EXPECT_EQ("sts-fips.us-west-1.amazonaws.com", Utility::getSTSEndpoint("us-west-1"));
  EXPECT_EQ("sts-fips.us-west-2.amazonaws.com", Utility::getSTSEndpoint("us-west-2"));
  // Even if FIPS mode is enabled ca-central-1 doesn't have a dedicated fips endpoint yet.
  EXPECT_EQ("sts.ca-central-1.amazonaws.com", Utility::getSTSEndpoint("ca-central-1"));
#else
  EXPECT_EQ("sts.us-east-1.amazonaws.com", Utility::getSTSEndpoint("us-east-1"));
  EXPECT_EQ("sts.us-east-2.amazonaws.com", Utility::getSTSEndpoint("us-east-2"));
  EXPECT_EQ("sts.us-west-1.amazonaws.com", Utility::getSTSEndpoint("us-west-1"));
  EXPECT_EQ("sts.us-west-2.amazonaws.com", Utility::getSTSEndpoint("us-west-2"));
  EXPECT_EQ("sts.ca-central-1.amazonaws.com", Utility::getSTSEndpoint("ca-central-1"));
#endif
}

// China regions: https://docs.aws.amazon.com/general/latest/gr/rande.html#sts_region.
TEST(UtilityTest, GetChinaSTSEndpoints) {
  EXPECT_EQ("sts.cn-north-1.amazonaws.com.cn", Utility::getSTSEndpoint("cn-north-1"));
  EXPECT_EQ("sts.cn-northwest-1.amazonaws.com.cn", Utility::getSTSEndpoint("cn-northwest-1"));
}

// GovCloud regions: https://docs.aws.amazon.com/general/latest/gr/rande.html#sts_region.
TEST(UtilityTest, GetGovCloudSTSEndpoints) {
  // No difference between fips vs non-fips endpoints in GovCloud.
  EXPECT_EQ("sts.us-gov-east-1.amazonaws.com", Utility::getSTSEndpoint("us-gov-east-1"));
  EXPECT_EQ("sts.us-gov-west-1.amazonaws.com", Utility::getSTSEndpoint("us-gov-west-1"));
}

// Test edge case where a SigV4a region set is provided and also web identity provider is in use
TEST(UtilityTest, CorrectlyConvertRegionSet) {
#ifdef ENVOY_SSL_FIPS
  EXPECT_EQ("sts-fips.us-east-1.amazonaws.com", Utility::getSTSEndpoint("*"));
  EXPECT_EQ("sts-fips.us-east-1.amazonaws.com", Utility::getSTSEndpoint("*,ap-southeast-2"));
  EXPECT_EQ("sts-fips.us-east-1.amazonaws.com",
            Utility::getSTSEndpoint("ca-central-*,ap-southeast-2"));
#else
  EXPECT_EQ("sts.amazonaws.com", Utility::getSTSEndpoint("*"));
  EXPECT_EQ("sts.amazonaws.com", Utility::getSTSEndpoint("*,ap-southeast-2"));
  EXPECT_EQ("sts.amazonaws.com", Utility::getSTSEndpoint("ca-central-*,ap-southeast-2"));
#endif
  EXPECT_EQ("sts.ap-southeast-2.amazonaws.com",
            Utility::getSTSEndpoint("ap-southeast-2,us-east-2"));
  EXPECT_EQ("sts.ca-central-1.amazonaws.com",
            Utility::getSTSEndpoint("ca-central-1,ap-southeast-2,eu-central-1"));
}

TEST(UtilityTest, JsonStringFound) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"access_key_id\":\"testvalue\"}");
  EXPECT_TRUE(test_json.ok());
  const auto expiration =
      Utility::getStringFromJsonOrDefault(test_json.value(), "access_key_id", "notfound");
  EXPECT_EQ(expiration, "testvalue");
}

TEST(UtilityTest, JsonStringNotFound) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"no_access_key_id\":\"testvalue\"}");
  EXPECT_TRUE(test_json.ok());
  const auto expiration =
      Utility::getStringFromJsonOrDefault(test_json.value(), "access_key_id", "notfound");
  EXPECT_EQ(expiration, "notfound");
}

TEST(UtilityTest, JsonIntegerFound) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"expiration\":5}");
  EXPECT_TRUE(test_json.ok());
  const auto expiration = Utility::getIntegerFromJsonOrDefault(test_json.value(), "expiration", 0);
  EXPECT_EQ(expiration, 5);
}

TEST(UtilityTest, JsonIntegerNotFound) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"noexpiration\":5}");
  EXPECT_TRUE(test_json.ok());
  const auto expiration = Utility::getIntegerFromJsonOrDefault(test_json.value(), "expiration", 0);
  // Should return default value
  EXPECT_EQ(expiration, 0);
}

// Check we handle double formatted integer > 0
TEST(UtilityTest, JsonIntegerExponent) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"expiration\":1.714449238E9}");
  EXPECT_TRUE(test_json.ok());
  auto value_or_error = test_json.value()->getValue("expiration");
  EXPECT_TRUE(value_or_error.ok());
  EXPECT_FALSE(absl::holds_alternative<int64_t>(value_or_error.value()));
  EXPECT_TRUE(absl::holds_alternative<double>(value_or_error.value()));
  const auto expiration = Utility::getIntegerFromJsonOrDefault(test_json.value(), "expiration", 0);
  // Should return default value
  EXPECT_EQ(expiration, 1714449238);
}

// Check we handle double formatted integer < 0
TEST(UtilityTest, JsonIntegerExponentInvalid) {
  auto test_json = Json::Factory::loadFromStringNoThrow("{\"expiration\":-0.17144492389}");
  EXPECT_TRUE(test_json.ok());
  auto value_or_error = test_json.value()->getValue("expiration");
  EXPECT_TRUE(value_or_error.ok());
  EXPECT_FALSE(absl::holds_alternative<int64_t>(value_or_error.value()));
  EXPECT_TRUE(absl::holds_alternative<double>(value_or_error.value()));
  const auto expiration =
      Utility::getIntegerFromJsonOrDefault(test_json.value(), "expiration", 9999);
  // Should return default value
  EXPECT_EQ(expiration, 9999);
}

TEST(UtilityTest, CheckNormalization) {
  std::string service = "s3";
  auto should_normalize = Utility::shouldNormalizeUriPath(service);
  EXPECT_FALSE(should_normalize);
  service = "s3-outposts";
  should_normalize = Utility::shouldNormalizeUriPath(service);
  EXPECT_FALSE(should_normalize);
  service = "s3-express";
  should_normalize = Utility::shouldNormalizeUriPath(service);
  EXPECT_FALSE(should_normalize);
  service = "vpc-lattice-svcs";
  should_normalize = Utility::shouldNormalizeUriPath(service);
  EXPECT_TRUE(should_normalize);
  service = "lambda";
  should_normalize = Utility::shouldNormalizeUriPath(service);
  EXPECT_TRUE(should_normalize);
}

} // namespace
} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
