#pragma once

#include "common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Common {
namespace Aws {

class Utility {
public:
  /**
   * Creates a canonicalized header map used in creating a AWS Signature V4 canonical request.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers a header map to canonicalize.
   * @return a std::map of canonicalized headers to be used in building a canonical request.
   */
  static std::map<std::string, std::string> canonicalizeHeaders(const Http::HeaderMap& headers);

  /**
   * Creates an AWS Signature V4 canonical request string.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param method the HTTP request method.
   * @param path the request path.
   * @param canonical_headers the pre-canonicalized request headers.
   * @param content_hash the hashed request body.
   * @return the canonicalized request string.
   */
  static std::string
  createCanonicalRequest(absl::string_view method, absl::string_view path,
                         const std::map<std::string, std::string>& canonical_headers,
                         absl::string_view content_hash);

  /**
   * Get the semicolon-delimited string of canonical header names.
   * @param canonical_headers the pre-canonicalized request headers.
   * @return the header names as a semicolon-delimited string.
   */
  static std::string
  joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers);
};

} // namespace Aws
} // namespace Common
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy