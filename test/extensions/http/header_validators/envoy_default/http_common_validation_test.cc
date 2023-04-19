#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/http1_header_validator.h"
#include "source/extensions/http/header_validators/envoy_default/http2_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::Protocol;
using ::Envoy::Http::TestRequestHeaderMapImpl;

enum class Protocols {
  Http1,
  Http2,
  Http3,
};

// This test suite runs the same tests against both H/1 and H/2 header validators.
class HttpCommonValidationTest : public HeaderValidatorTest,
                                 public testing::TestWithParam<Protocols> {
protected:
  ::Envoy::Http::HeaderValidatorPtr createUhv(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    if (GetParam() == Protocols::Http1) {
      return std::make_unique<Http1HeaderValidator>(typed_config, Protocol::Http11, stats_);
    }
    return std::make_unique<Http2HeaderValidator>(
        typed_config, GetParam() == Protocols::Http2 ? Protocol::Http2 : Protocol::Http3, stats_);
  }

  TestScopedRuntime scoped_runtime_;
};

std::string protocolTestParamsToString(const ::testing::TestParamInfo<Protocols>& params) {
  return params.param == Protocols::Http1   ? "Http1"
         : params.param == Protocols::Http2 ? "Http2"
                                            : "Http3";
}

INSTANTIATE_TEST_SUITE_P(Protocols, HttpCommonValidationTest,
                         testing::Values(Protocols::Http1, Protocols::Http2, Protocols::Http3),
                         protocolTestParamsToString);

TEST_P(HttpCommonValidationTest, MalformedUrlEncodingAllowed) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_allow_malformed_url_encoding", "true"}});
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path%Z%30with%xYbad%7Jencoding%"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_ACCEPT(uhv->transformRequestHeaders(headers));
  EXPECT_EQ(headers.path(), "/path%Z0with%xYbad%7Jencoding%");
}

TEST_P(HttpCommonValidationTest, MalformedUrlEncodingRejectedWithOverride) {
  scoped_runtime_.mergeValues(
      {{"envoy.reloadable_features.uhv_allow_malformed_url_encoding", "false"}});
  TestRequestHeaderMapImpl headers{{":scheme", "https"},
                                   {":path", "/path%Z%30with%xYbad%7Jencoding%A"},
                                   {":authority", "envoy.com"},
                                   {":method", "GET"}};
  auto uhv = createUhv(empty_config);

  EXPECT_ACCEPT(uhv->validateRequestHeaders(headers));
  EXPECT_REJECT_WITH_DETAILS(uhv->transformRequestHeaders(headers), "uhv.invalid_url");
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
