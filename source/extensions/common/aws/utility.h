#pragma once

#include "envoy/common/matchers.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/json/json_object.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class Utility : public Logger::Loggable<Logger::Id::aws> {
public:
  /**
   * Creates a canonicalized header map used in creating a AWS Signature V4 canonical request.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers a header map to canonicalize.
   * @param excluded_headers a list of string matchers to exclude a given header from signing.
   * @param included_headers a list of string matchers to include a given header from signing.
   * included_headers will take precedence over excluded_headers and if included_headers is
   * non-empty, only headers that match included_headers will be signed.
   * @return a std::map of canonicalized headers to be used in building a canonical request.
   */
  static std::map<std::string, std::string>
  canonicalizeHeaders(const Http::RequestHeaderMap& headers,
                      const std::vector<Matchers::StringMatcherPtr>& excluded_headers,
                      const std::vector<Matchers::StringMatcherPtr>& included_headers);

  /**
   * Creates an AWS Signature V4 canonical request string.
   * See https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param method the HTTP request method.
   * @param path the request path.
   * @param canonical_headers the pre-canonicalized request headers.
   * @param content_hash the hashed request body.
   * @param should_normalize_uri_path whether we should normalize the path string
   * @param use_double_uri_uncode whether we should perform an additional uri encode
   * @return the canonicalized request string.
   */
  static std::string
  createCanonicalRequest(absl::string_view method, absl::string_view path,
                         const std::map<std::string, std::string>& canonical_headers,
                         absl::string_view content_hash, bool should_normalize_uri_path,
                         bool use_double_uri_uncode);

  /**
   * Normalizes the path string based on AWS requirements.
   * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers header map
   */
  static std::string normalizePath(absl::string_view original_path);

  /**
   * URI encodes the path string based on AWS requirements.
   * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param headers header map
   */
  static std::string uriEncodePath(absl::string_view original_path);

  /**
   * Normalizes the query string based on AWS requirements.
   * See step 3 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
   * @param query_string the query string from the HTTP request.
   * @return the canonicalized query string.
   */
  static std::string canonicalizeQueryString(absl::string_view query_string);

  /**
   * URI encodes query component while preserving %2B semantics.
   * %2B remains as literal +, raw + becomes %20 (space).
   * @param original the original string (may contain percent-encoded sequences).
   * @return the properly encoded string.
   */
  static std::string encodeQueryComponentPreservingPlus(absl::string_view original);

  /**
   * Get the semicolon-delimited string of canonical header names.
   * @param canonical_headers the pre-canonicalized request headers.
   * @return the header names as a semicolon-delimited string.
   */
  static std::string
  joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers);

  /**
   * Get the IAM Roles Anywhere Service endpoint for a given region:
   * rolesanywhere.<region>.amazonaws.com See:
   * https://docs.aws.amazon.com/rolesanywhere/latest/userguide/authentication-sign-process.html#authentication-task1
   * @param trust_anchor_arn The configured roles anywhere trust anchor arn for the region to be
   * extracted from
   * @return an sts endpoint url.
   */

  static std::string getRolesAnywhereEndpoint(const std::string& trust_anchor_arn);

  /**
   * Get the Security Token Service endpoint for a given region: sts.<region>.amazonaws.com
   * See: https://docs.aws.amazon.com/general/latest/gr/rande.html#sts_region
   * @param region An AWS region.
   * @return an sts endpoint url.
   */
  static std::string getSTSEndpoint(absl::string_view region);

  /**
   * @brief Creates the prototype for a static cluster towards a credentials provider
   *        to fetch the credentials using http async client.
   *
   * @param cluster_name a name for credentials provider cluster
   * @param cluster_type STATIC or STRICT_DNS or LOGICAL_DNS etc
   * @param uri provider's IP (STATIC cluster) or URL (STRICT_DNS). Will use port 80 if the port is
   * not specified in the uri or no matching cluster is found.
   * @return the created cluster prototype
   * @return false if failed to add the cluster
   */
  static envoy::config::cluster::v3::Cluster
  createInternalClusterStatic(absl::string_view cluster_name,
                              const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type,
                              absl::string_view uri);

  /**
   * @brief Retrieve an environment variable if set, otherwise return default_value
   *
   * @param variable_name Environment variable.
   * @param default_value Value to be returned if environment variable is not set.
   * @return The evaluation result.
   */
  static std::string getEnvironmentVariableOrDefault(const std::string& variable_name,
                                                     const std::string& default_value);

  /**
   * @brief Given a profile name and a file containing profile elements, such as config or
   * credentials, retrieve all elements in the elements map
   *
   * @param profile_file path to the file to search for elements.
   * @param profile_name the profile section to search.
   * @param elements a hash map of elements to search for. values will be replaced if found.
   * @return true if profile file could be read and searched.
   * @return false if profile file could not be read.
   */

  static bool
  resolveProfileElementsFromString(const std::string& string_data, const std::string& profile_name,
                                   absl::flat_hash_map<std::string, std::string>& elements);

  static bool
  resolveProfileElementsFromFile(const std::string& profile_file, const std::string& profile_name,
                                 absl::flat_hash_map<std::string, std::string>& elements);

  static bool
  resolveProfileElementsFromStream(std::istream& stream, const std::string& profile_name,
                                   absl::flat_hash_map<std::string, std::string>& elements);

  /**
   * @brief Return the path of AWS credential file, following environment variable expansions
   *
   * @return File path of the AWS credential file.
   */
  static std::string getCredentialFilePath();

  /**
   * @brief Return the path of AWS config file, following environment variable expansions
   *
   * @return File path of the AWS config file.
   */
  static std::string getConfigFilePath();

  /**
   * @brief Return the AWS profile string within a config file, following environment variable
   * expansions
   *
   * @return Name of the profile string.
   */
  static std::string getConfigProfileName();

  /**
   * @brief Return the AWS profile string within a credentials file, following environment variable
   * expansions
   *
   * @return Name of the profile string.
   */
  static std::string getCredentialProfileName();

  /**
   * @brief Return a string value from a json object key, or a default it not found. Does not throw
   * exceptions.
   *
   * @return String value or default
   */
  static std::string getStringFromJsonOrDefault(Json::ObjectSharedPtr json_object,
                                                const std::string& string_value,
                                                const std::string& string_default);

  /**
   * @brief Return an integer value from a json object key, or a default it not found. Does not
   * throw exceptions. Will correctly handle json exponent formatted integers as well as standard
   * format.
   *
   * @return Integer value or default
   */
  static int64_t getIntegerFromJsonOrDefault(Json::ObjectSharedPtr json_object,
                                             const std::string& integer_value,
                                             const int64_t integer_default);

  /**
   * @brief Given a service name, should we uri encode the path
   * Logic for this is based on inspection of smithy definitions and currently only includes s3
   * services
   * @return boolean
   */

  static bool useDoubleUriEncode(const std::string service_name);

  /**
   * @brief Given a service name, should we normalize the path
   * Logic for this is based on inspection of smithy definitions and currently only includes s3
   * services
   * @return boolean
   */
  static bool shouldNormalizeUriPath(const std::string service_name);

  /**
   * Checks if a URI path is already percent-encoded according to RFC 3986.
   * Returns false if any character that should be percent-encoded is found unencoded.
   * @param path the URI path to check.
   * @return true if the path is already properly encoded, false otherwise.
   */
  static bool isUriPathEncoded(absl::string_view path);

private:
  /**
   * Helper method to encode a character based on reserved character rules.
   * @param c the character to encode.
   * @param result the string to append the encoded result to.
   */
  static void encodeCharacter(unsigned char c, std::string& result);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
