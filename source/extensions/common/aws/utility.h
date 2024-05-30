#pragma once

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options.pb.validate.h"
#include "envoy/http/message.h"

#include "source/common/common/matchers.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"

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
   * Get the Security Token Service endpoint for a given region: sts.<region>.amazonaws.com
   * See: https://docs.aws.amazon.com/general/latest/gr/rande.html#sts_region
   * @param region An AWS region.
   * @return an sts endpoint url.
   */
  static std::string getSTSEndpoint(absl::string_view region);

  /**
   * Fetch AWS instance or task metadata.
   *
   * @param message An HTTP request.
   * @return Metadata document or nullopt in case if unable to fetch it.
   *
   * @note In case of an error, function will log ENVOY_LOG_MISC(debug) message.
   *
   * @note This is not main loop safe method as it is blocking. It is intended to be used from the
   * gRPC auth plugins that are able to schedule blocking plugins on a different thread.
   */
  static absl::optional<std::string> fetchMetadata(Http::RequestMessage& message);

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
  static bool resolveProfileElements(const std::string& profile_file,
                                     const std::string& profile_name,
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
   * throw exceptions.
   *
   * @return Integer value or default
   */
  static int64_t getIntegerFromJsonOrDefault(Json::ObjectSharedPtr json_object,
                                             const std::string& integer_value,
                                             const int64_t integer_default);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
