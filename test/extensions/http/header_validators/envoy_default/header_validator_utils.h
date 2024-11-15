#pragma once

#include "envoy/http/header_validator.h"

#include "gtest/gtest.h"

#define EXPECT_REJECT(result) EXPECT_EQ(result.action(), decltype(result)::Action::Reject)
#define EXPECT_REJECT_WITH_DETAILS(result, details_value)                                          \
  {                                                                                                \
    auto __erwd_result = result;                                                                   \
    EXPECT_REJECT(__erwd_result);                                                                  \
    EXPECT_EQ(__erwd_result.details(), details_value);                                             \
  }                                                                                                \
  void(0)
#define EXPECT_ACCEPT(result)                                                                      \
  EXPECT_TRUE(result.ok()) << "rejected with details: " << result.details()

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

class HeaderValidatorUtils {
protected:
  void setHeaderStringUnvalidated(Envoy::Http::HeaderString& header_string,
                                  absl::string_view value) {
    header_string.setCopyUnvalidatedForTestOnly(value);
  }

  void validateAllCharactersInUrlPath(::Envoy::Http::ServerHeaderValidator& validator,
                                      absl::string_view path,
                                      absl::string_view additionally_allowed_characters);

  static constexpr absl::string_view empty_config = "{}";

  static constexpr absl::string_view restrict_http_methods_config = R"EOF(
    restrict_http_methods: true
)EOF";

  static constexpr absl::string_view reject_headers_with_underscores_config = R"EOF(
    headers_with_underscores_action: REJECT_REQUEST
)EOF";

  static constexpr absl::string_view drop_headers_with_underscores_config = R"EOF(
    headers_with_underscores_action: DROP_HEADER
)EOF";

  static constexpr absl::string_view allow_chunked_length_config = R"EOF(
    http1_protocol_options: {allow_chunked_length: true}
)EOF";

  static constexpr absl::string_view redirect_encoded_slash_config = R"EOF(
    uri_path_normalization_options:
      path_with_escaped_slashes_action: UNESCAPE_AND_REDIRECT
    )EOF";

  static constexpr absl::string_view no_path_normalization = R"EOF(
    uri_path_normalization_options:
      skip_path_normalization: true
      path_with_escaped_slashes_action: UNESCAPE_AND_REDIRECT
    )EOF";

  static constexpr absl::string_view fragment_in_path_allowed = R"EOF(
    strip_fragment_from_path: true
    )EOF";

  static constexpr absl::string_view no_path_normalization_no_decoding_slashes = R"EOF(
    uri_path_normalization_options:
      skip_path_normalization: true
      path_with_escaped_slashes_action: KEEP_UNCHANGED
    )EOF";

  static constexpr absl::string_view decode_slashes_and_forward = R"EOF(
    uri_path_normalization_options:
      skip_path_normalization: true
      path_with_escaped_slashes_action: UNESCAPE_AND_FORWARD
    )EOF";

  static constexpr absl::string_view reject_encoded_slashes = R"EOF(
    uri_path_normalization_options:
      skip_path_normalization: true
      path_with_escaped_slashes_action: REJECT_REQUEST
    )EOF";
};

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
