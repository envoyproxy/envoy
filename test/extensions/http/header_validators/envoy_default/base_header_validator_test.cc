#include "envoy/http/header_validator_errors.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"
#include "source/extensions/http/header_validators/envoy_default/header_validator.h"

#include "test/extensions/http/header_validators/envoy_default/header_validator_utils.h"
#include "test/mocks/http/header_validator.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;
using ::Envoy::Http::Protocol;
using ::Envoy::Http::testCharInTable;
using ::Envoy::Http::UhvResponseCodeDetail;

class BaseHeaderValidatorTest : public HeaderValidatorUtils, public testing::Test {
protected:
  std::unique_ptr<HeaderValidator> createBase(absl::string_view config_yaml) {
    envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig
        typed_config;
    TestUtility::loadFromYaml(std::string(config_yaml), typed_config);

    return std::make_unique<HeaderValidator>(typed_config, Protocol::Http11, stats_, overrides_);
  }
  ConfigOverrides overrides_;
  ::testing::NiceMock<Envoy::Http::MockHeaderValidatorStats> stats_;
};

TEST_F(BaseHeaderValidatorTest, ValidateMethodPermissive) {
  HeaderString valid{"GET"};
  HeaderString valid_lowercase{"post"};
  HeaderString custom{"Custom-Method"};
  auto uhv = createBase(empty_config);
  EXPECT_ACCEPT(uhv->validateMethodHeader(valid));
  EXPECT_ACCEPT(uhv->validateMethodHeader(valid_lowercase));
  EXPECT_ACCEPT(uhv->validateMethodHeader(custom));
}

TEST_F(BaseHeaderValidatorTest, ValidateMethodRestricted) {
  HeaderString valid{"GET"};
  HeaderString post_lowercase{"post"};
  HeaderString custom{"CUSTOM-METHOD"};
  auto uhv = createBase(restrict_http_methods_config);
  EXPECT_ACCEPT(uhv->validateMethodHeader(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateMethodHeader(custom),
                             UhvResponseCodeDetail::get().InvalidMethod);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateMethodHeader(post_lowercase),
                             UhvResponseCodeDetail::get().InvalidMethod);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeValid) {
  HeaderString valid_https{"https"};
  HeaderString valid_http{"http"};
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validateSchemeHeader(valid_https));
  EXPECT_ACCEPT(uhv->validateSchemeHeader(valid_http));

  HeaderString mixed_https{"HtTps"};
  HeaderString mixed_http{"hTtp"};

  EXPECT_ACCEPT(uhv->validateSchemeHeader(valid_https));
  EXPECT_ACCEPT(uhv->validateSchemeHeader(valid_http));
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidChar) {
  HeaderString invalid_char{"http_ssh"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid_char),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateSchemeInvalidStartChar) {
  HeaderString invalid_first_char{"+http"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateSchemeHeader(invalid_first_char),
                             UhvResponseCodeDetail::get().InvalidScheme);
}

TEST_F(BaseHeaderValidatorTest, ValidateResponseStatusRange) {
  HeaderString valid{"200"};
  HeaderString invalid_max{"1024"};
  HeaderString invalid_min{"99"};
  HeaderString invalid_overflow{"4294967297"}; // UINT32_MAX + 1
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validateStatusHeader(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_max),
                             UhvResponseCodeDetail::get().InvalidStatus);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_min),
                             UhvResponseCodeDetail::get().InvalidStatus);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateStatusHeader(invalid_overflow),
                             UhvResponseCodeDetail::get().InvalidStatus);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderName) {
  auto uhv = createBase(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);

    auto result = uhv->validateGenericHeaderName(header_string);
    if (testCharInTable(::Envoy::Http::kGenericHeaderNameCharTable, c)) {
      EXPECT_ACCEPT(result);
    } else if (c != '_') {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidUnderscore);
    }
  }
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyInvalidEmpty) {
  HeaderString invalid_empty{""};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateGenericHeaderName(invalid_empty),
                             UhvResponseCodeDetail::get().EmptyHeaderName);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderKeyRejectDropUnderscores) {
  HeaderString invalid_with_underscore{"x_fo<o"};
  auto uhv = createBase(drop_headers_with_underscores_config);

  auto result = uhv->validateGenericHeaderName(invalid_with_underscore);
  EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidNameCharacters);
}

TEST_F(BaseHeaderValidatorTest, ValidateGenericHeaderValue) {
  auto uhv = createBase(empty_config);
  std::string name{"aaaaa"};
  for (int i = 0; i < 0xff; ++i) {
    char c = static_cast<char>(i);
    HeaderString header_string{"x"};
    name[2] = c;

    setHeaderStringUnvalidated(header_string, name);

    auto result = uhv->validateGenericHeaderValue(header_string);
    if (testCharInTable(kGenericHeaderValueCharTable, c)) {
      EXPECT_ACCEPT(result);
    } else {
      EXPECT_REJECT_WITH_DETAILS(result, UhvResponseCodeDetail::get().InvalidValueCharacters);
    }
  }
}

TEST_F(BaseHeaderValidatorTest, ValidateContentLength) {
  HeaderString valid{"100"};
  HeaderString invalid{"10a2"};
  HeaderString invalid_overflow{"18446744073709551618"}; // UINT64_MAX + 1
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validateContentLengthHeader(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validateContentLengthHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidContentLength);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateContentLengthHeader(invalid_overflow),
                             UhvResponseCodeDetail::get().InvalidContentLength);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderValidRegName) {
  HeaderString valid{"envoy.com:443"};
  HeaderString valid_no_port{"envoy.com"};
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validateHostHeader(valid));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_no_port));
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidRegName) {
  HeaderString invalid{"env<o>y.com"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderValidIPv6) {
  HeaderString valid{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:443"};
  HeaderString valid_no_port{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]"};
  HeaderString valid_double_colon_all_0{"[::]"};
  HeaderString valid_double_colon{"[2001::7334]"};
  HeaderString valid_double_colon_at_beginning{"[::2001:7334]"};
  HeaderString valid_double_colon_at_end{"[2001:7334::]"};
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validateHostHeader(valid));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_no_port));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_double_colon_all_0));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_double_colon));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_double_colon_at_beginning));
  EXPECT_ACCEPT(uhv->validateHostHeader(valid_double_colon_at_end));
}

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidIPv6) {
  HeaderString invalid_missing_closing_bracket{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334"};
  HeaderString invalid_chars{"[200z:0db8:85a3:0000:0000:8a2e:0370:7334]"};
  HeaderString invalid_no_brackets{"200z:0db8:85a3:0000:0000:8a2e:0370:7334"};
  HeaderString invalid_more_than_8_parts{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334:1]:443"};
  HeaderString invalid_not_16_bits{"[1:1:20012:1:1:1:1:1]:443"};
  HeaderString invalid_2_double_colons{"[2::1::1]:443"};
  HeaderString invalid_2_double_colons_at_beginning{"[::1::1]:443"};
  HeaderString invalid_2_double_colons_at_end{"[1::1::]:443"};
  HeaderString invalid_2_double_colons_at_beginning_and_end{"[::1:1::]:443"};
  HeaderString invalid_3_colons{"[:::]:443"};
  HeaderString invalid_single_colon_at_end{"[1::1:]:443"};
  HeaderString invalid_single_colon_at_beginning{"[:1::1:2]:443"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_missing_closing_bracket),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_chars),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_no_brackets),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_more_than_8_parts),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_not_16_bits),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_2_double_colons),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_2_double_colons_at_beginning),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_2_double_colons_at_end),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_2_double_colons_at_beginning_and_end),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_3_colons),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_single_colon_at_end),
                             UhvResponseCodeDetail::get().InvalidHost);
  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_single_colon_at_beginning),
                             UhvResponseCodeDetail::get().InvalidHost);
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
                             UhvResponseCodeDetail::get().InvalidHostDeprecatedUserInfo);
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

TEST_F(BaseHeaderValidatorTest, ValidateHostHeaderInvalidIPv6PortDelim) {
  HeaderString invalid_port_delim{"[2001:0db8:85a3:0000:0000:8a2e:0370:7334]66000"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validateHostHeader(invalid_port_delim),
                             UhvResponseCodeDetail::get().InvalidHost);
}

TEST_F(BaseHeaderValidatorTest, ValidatePathHeaderCharacters) {
  HeaderString valid{"/parent/child"};
  HeaderString invalid{"/parent child"};
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validatePathHeaderCharacters(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validatePathHeaderCharacters(invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(BaseHeaderValidatorTest, ValidatePathHeaderCharactersQuery) {
  HeaderString valid{"/root?x=1"};
  HeaderString invalid{"/root?x=1|2"};
  auto uhv = createBase(empty_config);

  EXPECT_ACCEPT(uhv->validatePathHeaderCharacters(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validatePathHeaderCharacters(invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

TEST_F(BaseHeaderValidatorTest, PathWithFragmentRejectedByDefault) {
  HeaderString invalid{"/root?x=1#fragment"};
  auto uhv = createBase(empty_config);

  EXPECT_REJECT_WITH_DETAILS(uhv->validatePathHeaderCharacters(invalid),
                             UhvResponseCodeDetail::get().FragmentInUrlPath);
}

TEST_F(BaseHeaderValidatorTest, PathWithFragmentAllowedWhenConfigured) {
  HeaderString valid{"/root?x=1#fragment"};
  HeaderString invalid{"/root#frag|ment"};
  auto uhv = createBase(fragment_in_path_allowed);

  EXPECT_ACCEPT(uhv->validatePathHeaderCharacters(valid));
  EXPECT_REJECT_WITH_DETAILS(uhv->validatePathHeaderCharacters(invalid),
                             UhvResponseCodeDetail::get().InvalidUrl);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
