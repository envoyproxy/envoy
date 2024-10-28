#pragma once

#include "envoy/http/header_map.h"

#include "source/common/http/character_set_validation.h"
#include "source/common/http/status.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Http {
namespace HeaderValidation {

/**
 * Determines if request headers pass Envoy validity checks.
 * @param headers to validate
 * @return details of the error if an error is present, otherwise absl::nullopt
 */
absl::optional<std::reference_wrapper<const absl::string_view>>
requestHeadersValid(const RequestHeaderMap& headers);

/**
 * Validates that a header name is valid, according to RFC 7230, section 3.2.
 * http://tools.ietf.org/html/rfc7230#section-3.2
 * @return bool true if the header name is valid, according to the aforementioned RFC.
 */
bool headerNameIsValid(absl::string_view header_key);

/**
 * Validates that a header value is valid, according to RFC 7230, section 3.2.
 * http://tools.ietf.org/html/rfc7230#section-3.2
 * @return bool true if the header values are valid, according to the aforementioned RFC.
 */
bool headerValueIsValid(const absl::string_view header_value);

/**
 * Validates that the characters in the authority are valid.
 * @return bool true if the header values are valid, false otherwise.
 */
bool authorityIsValid(const absl::string_view authority_value);

/* Does a common header check ensuring that header keys and values are valid and do not contain
 * forbidden characters (e.g. valid HTTP header keys/values should never contain embedded NULLs
 * or new lines.)
 * @return Status containing the result. If failed, message includes details on which header key
 * or value was invalid.
 */
Http::Status checkValidRequestHeaders(const Http::RequestHeaderMap& headers);

#ifdef ENVOY_ENABLE_HTTP_DATAGRAMS
/**
 * @brief Returns true if the Capsule-Protocol header field (RFC 9297) is set to true. If the
 * header field is included multiple times, returns false as per RFC 9297.
 */
bool isCapsuleProtocol(const RequestOrResponseHeaderMap& headers);
#endif

} // namespace HeaderValidation
} // namespace Http
} // namespace Envoy
