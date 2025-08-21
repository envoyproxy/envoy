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
  const std::string FragmentInUrlPath = "uhv.fragment_in_url_path";
  const std::string EscapedSlashesInPath = "uhv.escaped_slashes_in_url_path";
  const std::string Percent00InPath = "uhv.percent_00_in_url_path";
};

using UhvResponseCodeDetail = ConstSingleton<UhvResponseCodeDetailValues>;

struct Http1ResponseCodeDetailValues {
  const std::string InvalidTransferEncoding = "http1.invalid_transfer_encoding";
  const std::string TransferEncodingNotAllowed = "uhv.http1.transfer_encoding_not_allowed";
  const std::string ContentLengthNotAllowed = "uhv.http1.content_length_not_allowed";
  const std::string ChunkedContentLength = "http1.content_length_and_chunked_not_allowed";
};

using Http1ResponseCodeDetail = ConstSingleton<Http1ResponseCodeDetailValues>;

struct PathNormalizerResponseCodeDetailValues {
  const std::string RedirectNormalized = "uhv.path_normalization_redirect";
};

using PathNormalizerResponseCodeDetail = ConstSingleton<PathNormalizerResponseCodeDetailValues>;

} // namespace Http
} // namespace Envoy
