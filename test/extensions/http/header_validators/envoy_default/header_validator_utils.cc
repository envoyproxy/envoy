#include "test/extensions/http/header_validators/envoy_default/header_validator_utils.h"

#include "source/extensions/http/header_validators/envoy_default/character_tables.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::Envoy::Http::HeaderString;

void HeaderValidatorUtils::validateAllCharactersInUrlPath(
    ::Envoy::Http::ServerHeaderValidator& validator, absl::string_view path,
    absl::string_view additionally_allowed_characters) {
  for (uint32_t ascii = 0x0; ascii <= 0xff; ++ascii) {
    std::string copy(path);
    copy[12] = static_cast<char>(ascii);
    HeaderString invalid_value{};
    setHeaderStringUnvalidated(invalid_value, copy);
    ::Envoy::Http::TestRequestHeaderMapImpl headers{
        {":scheme", "https"}, {":authority", "envoy.com"}, {":method", "GET"}};
    headers.addViaMove(HeaderString(absl::string_view(":path")), std::move(invalid_value));
    if (::Envoy::Http::testCharInTable(kPathHeaderCharTable, static_cast<char>(ascii)) ||
        absl::StrContains(additionally_allowed_characters, static_cast<char>(ascii))) {
      EXPECT_ACCEPT(validator.validateRequestHeaders(headers))
          << " for character " << static_cast<char>(ascii) << " " << ascii;
    } else {
      auto result = validator.validateRequestHeaders(headers);
      EXPECT_REJECT(result) << " for character " << static_cast<char>(ascii) << " " << ascii;
      EXPECT_EQ(result.details(), "uhv.invalid_url")
          << " for character " << static_cast<char>(ascii) << " " << ascii;
    }
  }
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
