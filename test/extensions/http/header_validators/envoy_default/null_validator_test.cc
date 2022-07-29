#include "source/extensions/http/header_validators/envoy_default/null_header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {
namespace {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::HeaderValidator;
using ::Envoy::Http::Protocol;

class NullHeaderValidatorTest : public HeaderValidatorTest {
protected:
  NullHeaderValidatorPtr createNull(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<NullHeaderValidator>(typed_config, Protocol::Http3, stream_info_);
  }
};

TEST_F(NullHeaderValidatorTest, RequestHeaderNameValidation) {
  auto uhv = createNull(empty_config);
  // Since the default UHV does not yet check anything all header values should be accepted
  std::string key_value("aaa");
  HeaderString key(key_value);
  HeaderString value("valid");
  for (int c = 0; c <= 0xff; ++c) {
    key_value[1] = c;
    setHeaderStringUnvalidated(key, key_value);
    EXPECT_TRUE(uhv->validateRequestHeaderEntry(key, value).ok());
  }
}

TEST_F(NullHeaderValidatorTest, ResponseHeaderNameValidation) {
  auto uhv = createNull(empty_config);
  // Since the default UHV does not yet check anything all header values should be accepted
  std::string key_value("aaa");
  HeaderString key(key_value);
  HeaderString value("valid");
  for (int c = 0; c <= 0xff; ++c) {
    key_value[1] = c;
    setHeaderStringUnvalidated(key, key_value);
    EXPECT_TRUE(uhv->validateResponseHeaderEntry(key, value).ok());
  }
}

TEST_F(NullHeaderValidatorTest, RequestHeaderMapValidation) {
  auto uhv = createNull(empty_config);
  ::Envoy::Http::TestRequestHeaderMapImpl request_header_map{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  EXPECT_TRUE(uhv->validateRequestHeaderMap(request_header_map).ok());
}

TEST_F(NullHeaderValidatorTest, ResponseHeaderMapValidation) {
  auto uhv = createNull(empty_config);
  ::Envoy::Http::TestResponseHeaderMapImpl response_header_map{{":status", "200"}};
  EXPECT_TRUE(uhv->validateResponseHeaderMap(response_header_map).ok());
}

} // namespace
} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
