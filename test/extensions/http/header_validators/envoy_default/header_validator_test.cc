#include "test/extensions/http/header_validators/envoy_default/header_validator_test.h"

#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::RequestHeaderMap;
using ::Envoy::Http::ResponseHeaderMap;

class BaseHttpHeaderValidator : public HeaderValidator {
public:
  BaseHttpHeaderValidator(
      const envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig&
          config,
      Protocol protocol, StreamInfo::StreamInfo& stream_info)
      : HeaderValidator(config, protocol, stream_info) {}

  HeaderEntryValidationResult validateRequestHeaderEntry(const HeaderString&,
                                                         const HeaderString&) override {
    return HeaderEntryValidationResult::success();
  }

  HeaderEntryValidationResult validateResponseHeaderEntry(const HeaderString&,
                                                          const HeaderString&) override {
    return HeaderEntryValidationResult::success();
  }

  RequestHeaderMapValidationResult validateRequestHeaderMap(RequestHeaderMap&) override {
    return RequestHeaderMapValidationResult::success();
  }

  ResponseHeaderMapValidationResult validateResponseHeaderMap(ResponseHeaderMap&) override {
    return ResponseHeaderMapValidationResult::success();
  }
};

using BaseHttpHeaderValidatorPtr = std::unique_ptr<BaseHttpHeaderValidator>;

class BaseHeaderValidatorTest : public HeaderValidatorTest {
protected:
  BaseHttpHeaderValidatorPtr createBase(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<BaseHttpHeaderValidator>(typed_config, Protocol::Http11, stream_info_);
  }
};

TEST_F(BaseHeaderValidatorTest, ValidateMethodPermissive) {
  HeaderString valid{"GET"};
  HeaderString custom{"CUSTOM-METHOD"};
  auto uhv = createBase(empty_config);
  EXPECT_TRUE(uhv->validateMethodHeader(valid).ok());
  EXPECT_TRUE(uhv->validateMethodHeader(custom).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateMethodStrict) {
  HeaderString valid{"GET"};
  HeaderString custom{"CUSTOM-METHOD"};
  auto uhv = createBase(restrict_http_methods_config);
  EXPECT_TRUE(uhv->validateMethodHeader(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateMethodHeader(custom),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeValid) {
  HeaderString valid{"https"};
  HeaderString valid_mixed_case{"hTtPs"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateSchemeHeader(valid).ok());
  EXPECT_TRUE(uhv->validateSchemeHeader(valid_mixed_case).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidChar) {
  HeaderString invalid{"http_ssh"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidStartChar) {
  HeaderString invalid_first_char{"+http"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid_first_char),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusNoneValid) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::WholeNumber;
  HeaderString valid{"200"};
  HeaderString valid_outside_of_range{"1024"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateStatusHeader(mode, valid).ok());
  EXPECT_TRUE(uhv->validateStatusHeader(mode, valid_outside_of_range).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusNoneInvalid) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::WholeNumber;
  HeaderString invalid{"asdf"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusRangeValid) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::ValueRange;
  HeaderString valid{"200"};
  HeaderString invalid_max{"1024"};
  HeaderString invalid_min{"99"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateStatusHeader(mode, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid_max),
                             UhvResponseCodeDetail::get().InvalidStatus);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid_min),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusRangeInvalidMin) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::ValueRange;
  HeaderString invalid_min{"99"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid_min),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusRangeInvalidMax) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::ValueRange;
  HeaderString invalid_max{"1024"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid_max),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusOfficalCodes) {
  auto mode = BaseHttpHeaderValidator::StatusPseudoHeaderValidationMode::OfficialStatusCodes;
  HeaderString valid{"200"};
  HeaderString invalid_unregistered{"420"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateStatusHeader(mode, valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(mode, invalid_unregistered),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderNameValid) {
  HeaderString valid{"x-foo"};
  auto uhv = createBase(reject_headers_with_underscores_config);

  EXPECT_TRUE(uhv->validateGenericHeaderName(valid).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyRejectUnderscores) {
  HeaderString invalid_underscore{"x_foo"};
  auto uhv = createBase(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_underscore),
                             UhvResponseCodeDetail::get().InvalidUnderscore);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyInvalidChar) {
  HeaderString invalid_eascii{"x-foo\x80"};
  auto uhv = createBase(reject_headers_with_underscores_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_eascii),
                             UhvResponseCodeDetail::get().InvalidCharacters);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyStrictValid) {
  HeaderString valid{"x-foo"};
  HeaderString valid_underscore{"x_foo"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateGenericHeaderName(valid).ok());
  EXPECT_TRUE(uhv->validateGenericHeaderName(valid_underscore).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyStrictInvalidChar) {
  HeaderString invalid_eascii{"x-foo\x80"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_eascii),
                             UhvResponseCodeDetail::get().InvalidCharacters);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyStrictInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_empty),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderValue) {
  HeaderString valid{"hello world"};
  HeaderString valid_eascii{"value\x80"};
  HeaderString invalid_newline;
  auto uhv = createBase(empty_config);

  setHeaderStringUnvalidated(invalid_newline, "hello\nworld");

  EXPECT_TRUE(uhv->validateGenericHeaderValue(valid).ok());
  EXPECT_TRUE(uhv->validateGenericHeaderValue(valid_eascii).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderValue(invalid_newline),
                             UhvResponseCodeDetail::get().InvalidCharacters);
}

TEST_F(BaseHeaderValidatorTest, ValidateContentLength) {
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateContentLengthHeader(valid).ok());
  EXPECT_REJECT_WITH_DETAILS(uhv->validateContentLengthHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderValid) {
  HeaderString valid{"envoy.com:443"};
  HeaderString valid_no_port{"envoy.com"};
  auto uhv = createBase(empty_config);

  EXPECT_TRUE(uhv->validateHostHeader(valid).ok());
  EXPECT_TRUE(uhv->validateHostHeader(valid_no_port).ok());
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_empty),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidUserInfo) {
  HeaderString invalid_userinfo{"foo:bar@envoy.com"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_userinfo),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortNumber) {
  HeaderString invalid_port_int{"envoy.com:a"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_int),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortTrailer) {
  HeaderString invalid_port_trailer{"envoy.com:10a"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_trailer),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPortMax) {
  HeaderString invalid_port_value{"envoy.com:66000"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_value),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidPort0) {
  HeaderString invalid_port_0{"envoy.com:0"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_0),
                             UhvResponseCodeDetail::get().InvalidHost);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
