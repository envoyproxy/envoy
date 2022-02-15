#pragma once

#include "source/common/common/matchers.h"
#include "source/common/http/headers.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class Utility {
public:
  /**
   * Creates a canonicalized header map used in creating a AWS Signature V4 canonical request.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers a header map to canonicalize.
   * @param excluded_headers a list of string matchers to exclude a given header from signing.
   * @return a std::map of canonicalized headers to be used in building a canonical request.
   */
  static std::map<std::string, std::string>
  canonicalizeHeaders(const Http::RequestHeaderMap& headers,
                      const std::vector<Matchers::StringMatcherPtr>& excluded_headers);

  /**
   * Creates an AWS Signature V4 canonical request string.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param service_name the AWS service name.
   * @param method the HTTP request method.
   * @param path the request path.
   * @param canonical_headers the pre-canonicalized request headers.
   * @param content_hash the hashed request body.
   * @return the canonicalized request string.
   */
  static std::string createCanonicalRequest(
      absl::string_view service_name, absl::string_view method, absl::string_view path,
      const std::map<std::string, std::string>& canonical_headers, absl::string_view content_hash);

  /**
   * Normalizes the path string based on AWS requirements.
   * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param query_string the query string from the HTTP request.
   * @param service_name the AWS service name.
   * @return the canonicalized query string.
   */
  static std::string canonicalizePathString(absl::string_view path_string,
                                            absl::string_view service_name);

  /**
   * URI encodes the given string based on AWS requirements.
   * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param decoded the decoded string.
   * @param service_name the AWS service name.
   * @return the URI encoded string.
   */
  static std::string encodePathSegment(absl::string_view decoded, absl::string_view service_name);

  /**
   * Normalizes the query string based on AWS requirements.
   * See step 3 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param query_string the query string from the HTTP request.
   * @return the canonicalized query string.
   */
  static std::string canonicalizeQueryString(absl::string_view query_string);

  /**
   * URI encodes the given string based on AWS requirements.
   * See step 3 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param decoded the decoded string.
   * @return the URI encoded string.
   */
  static std::string encodeQueryParam(absl::string_view decoded);

  /**
   * Get the semicolon-delimited string of canonical header names.
   * @param canonical_headers the pre-canonicalized request headers.
   * @return the header names as a semicolon-delimited string.
   */
  static std::string
  joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers);

  /**
   * Fetch AWS instance or task metadata.
   *
   * @param host host or ip address of the metadata endpoint.
   * @param path path of the metadata document.
   * @auth_token authentication token to pass in the request, empty string indicates no auth.
   * @return Metadata document or nullopt in case if unable to fetch it.
   *
   * @note In case of an error, function will log ENVOY_LOG_MISC(debug) message.
   *
   * @note This is not main loop safe method as it is blocking. It is intended to be used from the
   * gRPC auth plugins that are able to schedule blocking plugins on a different thread.
   */
  static absl::optional<std::string>
  metadataFetcher(const std::string& host, const std::string& path, const std::string& auth_token);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
