#pragma once

#include <string>

#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Http {

struct UhvResponseCodeDetailValues {
  const std::string InvalidNameCharacters = "uhv.invalid_name_characters";
  const std::string InvalidValueCharacters = "uhv.invalid_value_characters";
  const std::string InvalidUrl = "uhv.invalid_url";
  const std::string InvalidHost = "uhv.invalid_host";
  const std::string InvalidScheme = "uhv.invalid_scheme";
  const std::string InvalidMethod = "uhv.invalid_method";
  const std::string InvalidContentLength = "uhv.invalid_content_length";
  const std::string InvalidUnderscore = "uhv.unexpected_underscore";
  const std::string InvalidStatus = "uhv.invalid_status";
  const std::string EmptyHeaderName = "uhv.empty_header_name";
  const std::string InvalidPseudoHeader = "uhv.invalid_pseudo_header";
  const std::string InvalidHostDeprecatedUserInfo = "uhv.invalid_host_deprecated_user_info";
};

using UhvResponseCodeDetail = ConstSingleton<UhvResponseCodeDetailValues>;

} // namespace Http
} // namespace Envoy
