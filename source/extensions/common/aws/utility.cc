#include "source/extensions/common/aws/utility.h"

#include <cstdint>
#include <limits>

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/common/aws/signer_base_impl.h"

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "curl/curl.h"
#include "fmt/printf.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

constexpr absl::string_view PATH_SPLITTER = "/";
constexpr absl::string_view QUERY_PARAM_SEPERATOR = "=";
constexpr absl::string_view QUERY_SEPERATOR = "&";
constexpr absl::string_view QUERY_SPLITTER = "?";
constexpr absl::string_view RESERVED_CHARS = "-._~";
constexpr absl::string_view URI_ENCODE = "%{:02X}";

constexpr char AWS_SHARED_CREDENTIALS_FILE[] = "AWS_SHARED_CREDENTIALS_FILE";
constexpr char AWS_PROFILE[] = "AWS_PROFILE";
constexpr char DEFAULT_AWS_SHARED_CREDENTIALS_FILE[] = "/.aws/credentials";
constexpr char DEFAULT_AWS_PROFILE[] = "default";
constexpr char AWS_CONFIG_FILE[] = "AWS_CONFIG_FILE";
constexpr char DEFAULT_AWS_CONFIG_FILE[] = "/.aws/config";

std::map<std::string, std::string>
Utility::canonicalizeHeaders(const Http::RequestHeaderMap& headers,
                             const std::vector<Matchers::StringMatcherPtr>& excluded_headers) {
  std::map<std::string, std::string> out;
  headers.iterate(
      [&out, &excluded_headers](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
        // Skip empty headers
        if (entry.key().empty() || entry.value().empty()) {
          return Http::HeaderMap::Iterate::Continue;
        }
        // Pseudo-headers should not be canonicalized
        if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
          return Http::HeaderMap::Iterate::Continue;
        }
        const auto key = entry.key().getStringView();
        if (std::any_of(excluded_headers.begin(), excluded_headers.end(),
                        [&key](const Matchers::StringMatcherPtr& matcher) {
                          return matcher->match(key);
                        })) {
          return Http::HeaderMap::Iterate::Continue;
        }

        std::string value(entry.value().getStringView());
        // Remove leading, trailing, and deduplicate repeated ascii spaces
        absl::RemoveExtraAsciiWhitespace(&value);
        const auto iter = out.find(std::string(entry.key().getStringView()));
        // If the entry already exists, append the new value to the end
        if (iter != out.end()) {
          iter->second += fmt::format(",{}", value);
        } else {
          out.emplace(std::string(entry.key().getStringView()), value);
        }
        return Http::HeaderMap::Iterate::Continue;
      });
  // The AWS SDK has a quirk where it removes "default ports" (80, 443) from the host headers
  // Additionally, we canonicalize the :authority header as "host"
  // TODO(suniltheta): This may need to be tweaked to canonicalize :authority for HTTP/2 requests
  const absl::string_view authority_header = headers.getHostValue();
  if (!authority_header.empty()) {
    const auto parts = StringUtil::splitToken(authority_header, ":");
    if (parts.size() > 1 && (parts[1] == "80" || parts[1] == "443")) {
      // Has default port, so use only the host part
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(parts[0]));
    } else {
      out.emplace(Http::Headers::get().HostLegacy.get(), std::string(authority_header));
    }
  }
  return out;
}

std::string
Utility::createCanonicalRequest(absl::string_view method, absl::string_view path,
                                const std::map<std::string, std::string>& canonical_headers,
                                absl::string_view content_hash, bool should_normalize_uri_path,
                                bool use_double_uri_uncode) {

  std::string canonical_request;

  // Add the method
  canonical_request = absl::StrCat(method, "\n");

  // don't include the query part of the path
  const auto path_part = StringUtil::cropRight(path, QUERY_SPLITTER);

  std::string new_path;
  if (use_double_uri_uncode) {
    if (should_normalize_uri_path) {
      new_path = normalizePath(path_part);
    } else {
      new_path = path_part;
    }
    new_path = uriEncodePath(new_path);
  } else {
    if (should_normalize_uri_path) {
      new_path = normalizePath(path_part);
    } else {
      new_path = path_part;
    }
  }

  // No path present, so add /
  if (new_path.empty()) {
    absl::StrAppend(&canonical_request, PATH_SPLITTER, "\n");
  } else {
    // Append path verbatim
    absl::StrAppend(&canonical_request, new_path, "\n");
  }

  const auto query_part = StringUtil::cropLeft(path, QUERY_SPLITTER);

  // If query_part == path_part, then we have no query string. Otherwise canonicalize it.
  if (query_part == path_part) {
    absl::StrAppend(&canonical_request, "\n");
  } else {
    absl::StrAppend(&canonical_request, canonicalizeQueryString(query_part), "\n");
  }

  // Add headers
  std::vector<std::string> formatted_headers;
  formatted_headers.reserve(canonical_headers.size());
  for (const auto& header : canonical_headers) {
    formatted_headers.emplace_back(fmt::format("{}:{}", header.first, header.second));
    absl::StrAppend(&canonical_request, formatted_headers.back(), "\n");
  }

  // Add blank space after the canonical headers
  absl::StrAppend(&canonical_request, "\n");

  // Add the list of signed headers
  absl::StrAppend(&canonical_request, joinCanonicalHeaderNames(canonical_headers), "\n");

  // Add the content hash
  absl::StrAppend(&canonical_request, content_hash);

  return canonical_request;
}

/**
 * Normalizes the path string based on AWS requirements.
 * See step 2 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
 */
std::string Utility::normalizePath(absl::string_view original_path) {

  const auto path_segments = StringUtil::splitToken(original_path, std::string{PATH_SPLITTER});
  std::vector<std::string> path_list;
  path_list.reserve(path_segments.size());

  /* Loop through path segment.
   *
   * If segment is blank or . then skip it
   * If segment is .. then remove the previous path segment
   * Otherwise append the path segment as normal
   */
  for (const auto& path_segment : path_segments) {
    if (path_segment.empty() || path_segment == ".") {
      continue;
    } else if (path_segment == "..") {
      if (path_list.empty()) {
        path_list.emplace_back(PATH_SPLITTER);
      } else {
        path_list.pop_back();
      }
    } else {
      path_list.emplace_back(path_segment);
    }
  }

  auto canonical_path_string =
      fmt::format("{}{}", PATH_SPLITTER, absl::StrJoin(path_list, PATH_SPLITTER));
  // Handle corner case when path ends with '/'
  if (absl::EndsWith(original_path, PATH_SPLITTER) && canonical_path_string.size() > 1) {
    canonical_path_string.push_back(PATH_SPLITTER[0]);
  }
  return canonical_path_string;
}

bool isReservedChar(const char c) {
  return std::isalnum(c) || RESERVED_CHARS.find(c) != std::string::npos;
}

std::string Utility::uriEncodePath(absl::string_view original_path) {

  const absl::string_view::size_type query_start = original_path.find_first_of("?#");
  const absl::string_view path = original_path.substr(0, query_start);
  const absl::string_view query = absl::ClippedSubstr(original_path, query_start);

  std::string encoded;

  for (unsigned char c : path) {
    // Do not encode slashes or unreserved chars from RFC 3986
    if ((isReservedChar(c)) || c == PATH_SPLITTER[0]) {
      encoded.push_back(c);
    } else {
      absl::StrAppend(&encoded, fmt::format(URI_ENCODE, c));
    }
  }

  absl::StrAppend(&encoded, query);

  return encoded;
}

/**
 * Normalizes the query string based on AWS requirements.
 * See step 3 in https://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html
 */
std::string Utility::canonicalizeQueryString(absl::string_view query_string) {
  // Sort query string based on param name and append "=" if value is missing
  const auto query_fragments = StringUtil::splitToken(query_string, QUERY_SEPERATOR);
  std::vector<std::pair<std::string, std::string>> query_list;
  for (const auto& query_fragment : query_fragments) {
    // Only split at the first "=" and encode the rest
    const std::vector<std::string> query =
        absl::StrSplit(query_fragment, absl::MaxSplits(QUERY_PARAM_SEPERATOR, 1));
    if (!query.empty()) {
      const absl::string_view param = query[0];
      const absl::string_view value = query.size() > 1 ? query[1] : EMPTY_STRING;
      query_list.emplace_back(std::make_pair(param, value));
    }
  }

  // Encode query params name and value separately
  for (auto& query : query_list) {
    // The token has already been url encoded, so don't do it again
    if (query.first == SignatureQueryParameterValues::AmzSecurityToken) {
      query = std::make_pair(query.first, query.second);
    } else {
      query = std::make_pair(
          encodeQueryComponent(Envoy::Http::Utility::PercentEncoding::decode(query.first)),
          encodeQueryComponent(Envoy::Http::Utility::PercentEncoding::decode(query.second)));
    }
  }

  // Sort query params by name and value after encoding
  std::sort(query_list.begin(), query_list.end());

  return absl::StrJoin(query_list, QUERY_SEPERATOR, absl::PairFormatter(QUERY_PARAM_SEPERATOR));
}

// To avoid modifying the path, we handle spaces as if they have already been encoded to a plus, and
// avoid additional equals signs in the query parameters
std::string Utility::encodeQueryComponent(absl::string_view decoded) {
  std::string encoded;
  for (unsigned char c : decoded) {
    if (isReservedChar(c)) {
      // Escape unreserved chars from RFC 3986
      encoded.push_back(c);
    } else if (c == '+') {
      // Encode '+' as space
      absl::StrAppend(&encoded, "%20");
    } else {
      absl::StrAppend(&encoded, fmt::format(URI_ENCODE, c));
    }
  }
  return encoded;
}

std::string
Utility::joinCanonicalHeaderNames(const std::map<std::string, std::string>& canonical_headers) {
  return absl::StrJoin(canonical_headers, ";", [](auto* out, const auto& pair) {
    return absl::StrAppend(out, pair.first);
  });
}

/**
 * This function generates an STS Endpoint from a region string.
 * If a SigV4A region set has been provided, it will use the first region in region set, and if
 * that region still contains a wildcard, the STS Endpoint will be set to us-east-1 global endpoint
 * (or FIPS if compiled for FIPS support)
 */
std::string Utility::getSTSEndpoint(absl::string_view region) {
  std::string single_region;

  // If we contain a comma or asterisk it looks like a region set.
  if (absl::StrContains(region, ",") || (absl::StrContains(region, "*"))) {
    // Use the first element from a region set if we have multiple regions specified.
    const std::vector<std::string> region_v = absl::StrSplit(region, ',');
    // If we still have a * in the first element, then send them to us-east-1 fips or global
    // endpoint.
    if (absl::StrContains(region_v[0], '*')) {
#ifdef ENVOY_SSL_FIPS
      return "sts-fips.us-east-1.amazonaws.com";
#else
      return "sts.amazonaws.com";
#endif
    }
    single_region = region_v[0];
  } else {
    // Otherwise it's a standard region, so use that.
    single_region = region;
  }

  if (single_region == "cn-northwest-1" || single_region == "cn-north-1") {
    return fmt::format("sts.{}.amazonaws.com.cn", single_region);
  }
#ifdef ENVOY_SSL_FIPS
  // Use AWS STS FIPS endpoints in FIPS mode https://docs.aws.amazon.com/general/latest/gr/sts.html.
  // Note: AWS GovCloud doesn't have separate fips endpoints.
  // TODO(suniltheta): Include `ca-central-1` when sts supports a dedicated FIPS endpoint.
  if (single_region == "us-east-1" || single_region == "us-east-2" ||
      single_region == "us-west-1" || single_region == "us-west-2") {
    return fmt::format("sts-fips.{}.amazonaws.com", single_region);
  }
#endif
  return fmt::format("sts.{}.amazonaws.com", single_region);
}

static size_t curlCallback(char* ptr, size_t, size_t nmemb, void* data) {
  auto buf = static_cast<std::string*>(data);
  buf->append(ptr, nmemb);
  return nmemb;
}

absl::optional<std::string> Utility::fetchMetadata(Http::RequestMessage& message) {
  static const size_t MAX_RETRIES = 4;
  static const std::chrono::milliseconds RETRY_DELAY{1000};
  static const std::chrono::seconds TIMEOUT{5};

  CURL* const curl = curl_easy_init();
  if (!curl) {
    return absl::nullopt;
  };

  const auto host = message.headers().getHostValue();
  const auto path = message.headers().getPathValue();
  const auto method = message.headers().getMethodValue();
  const auto scheme = message.headers().getSchemeValue();

  const std::string url = fmt::format("{}://{}{}", scheme, host, path);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT.count());
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  std::string buffer;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlCallback);

  struct curl_slist* headers = nullptr;
  message.headers().iterate([&headers](const Http::HeaderEntry& entry) -> Http::HeaderMap::Iterate {
    // Skip pseudo-headers
    if (!entry.key().getStringView().empty() && entry.key().getStringView()[0] == ':') {
      return Http::HeaderMap::Iterate::Continue;
    }
    const std::string header =
        fmt::format("{}: {}", entry.key().getStringView(), entry.value().getStringView());
    headers = curl_slist_append(headers, header.c_str());
    return Http::HeaderMap::Iterate::Continue;
  });

  // This function only support doing PUT(UPLOAD) other than GET(_default_) operation.
  if (Http::Headers::get().MethodValues.Put == method) {
    // https://curl.se/libcurl/c/CURLOPT_PUT.html is deprecated
    // so using https://curl.se/libcurl/c/CURLOPT_UPLOAD.html.
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    // To call PUT on HTTP 1.0 we must specify a value for the upload size
    // since some old EC2's metadata service will be serving on HTTP 1.0.
    // https://curl.se/libcurl/c/CURLOPT_INFILESIZE.html
    curl_easy_setopt(curl, CURLOPT_INFILESIZE, 0);
    // Disabling `Expect: 100-continue` header to get a response
    // in the first attempt as the put size is zero.
    // https://everything.curl.dev/http/post/expect100
    headers = curl_slist_append(headers, "Expect:");
  }

  if (headers != nullptr) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  for (size_t retry = 0; retry < MAX_RETRIES; retry++) {
    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
      break;
    }
    ENVOY_LOG_MISC(debug, "Could not fetch AWS metadata: {}", curl_easy_strerror(res));
    buffer.clear();
    std::this_thread::sleep_for(RETRY_DELAY);
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);

  return buffer.empty() ? absl::nullopt : absl::optional<std::string>(buffer);
}

envoy::config::cluster::v3::Cluster Utility::createInternalClusterStatic(
    absl::string_view cluster_name,
    const envoy::config::cluster::v3::Cluster::DiscoveryType cluster_type, absl::string_view uri) {
  // Check if local cluster exists with that name.

  envoy::config::cluster::v3::Cluster cluster;
  absl::string_view host_port;
  absl::string_view path;
  Http::Utility::extractHostPathFromUri(uri, host_port, path);
  const auto host_attributes = Http::Utility::parseAuthority(host_port);
  const auto host = host_attributes.host_;
  const auto port = host_attributes.port_ ? host_attributes.port_.value() : 80;

  cluster.set_name(cluster_name);
  cluster.set_type(cluster_type);
  cluster.mutable_connect_timeout()->set_seconds(5);
  cluster.mutable_load_assignment()->set_cluster_name(cluster_name);
  auto* endpoint =
      cluster.mutable_load_assignment()->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  auto* addr = endpoint->mutable_address();
  addr->mutable_socket_address()->set_address(host);
  addr->mutable_socket_address()->set_port_value(port);
  cluster.set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions protocol_options;
  auto* http_protocol_options =
      protocol_options.mutable_explicit_http_config()->mutable_http_protocol_options();
  http_protocol_options->set_accept_http_10(true);
  (*cluster.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(protocol_options);

  // Add tls transport socket if cluster supports https over port 443.
  if (port == 443) {
    auto* socket = cluster.mutable_transport_socket();
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_socket;
    socket->set_name("envoy.transport_sockets.tls");
    socket->mutable_typed_config()->PackFrom(tls_socket);
  }

  return cluster;
}

std::string Utility::getEnvironmentVariableOrDefault(const std::string& variable_name,
                                                     const std::string& default_value) {
  const char* value = getenv(variable_name.c_str());
  return (value != nullptr) && (value[0] != '\0') ? value : default_value;
}

bool Utility::resolveProfileElements(const std::string& profile_file,
                                     const std::string& profile_name,
                                     absl::flat_hash_map<std::string, std::string>& elements) {
  std::ifstream file(profile_file);
  if (!file.is_open()) {
    ENVOY_LOG_MISC(debug, "Error opening credentials file {}", profile_file);
    return false;
  }
  const auto profile_start = absl::StrFormat("[%s]", profile_name);

  bool found_profile = false;
  std::string line;
  while (std::getline(file, line)) {
    line = std::string(StringUtil::trim(line));
    if (line.empty()) {
      continue;
    }

    if (line == profile_start) {
      found_profile = true;
      continue;
    }

    if (found_profile) {
      // Stop reading once we find the start of the next profile.
      if (absl::StartsWith(line, "[")) {
        break;
      }

      std::vector<std::string> parts = absl::StrSplit(line, absl::MaxSplits('=', 1));
      if (parts.size() == 2) {

        const auto key = StringUtil::toUpper(StringUtil::trim(parts[0]));
        const auto val = StringUtil::trim(parts[1]);
        auto found = elements.find(key);
        if (found != elements.end()) {
          found->second = val;
        }
      }
    }
  }
  return true;
}

std::string Utility::getCredentialFilePath() {

  // Default credential file path plus current home directory. Will fall back to / if HOME
  // environment variable does
  // not exist

  const auto home = getEnvironmentVariableOrDefault("HOME", "");
  const auto default_credentials_file_path =
      absl::StrCat(home, DEFAULT_AWS_SHARED_CREDENTIALS_FILE);

  return getEnvironmentVariableOrDefault(AWS_SHARED_CREDENTIALS_FILE,
                                         default_credentials_file_path);
}

std::string Utility::getConfigFilePath() {

  // Default config file path plus current home directory. Will fall back to / if HOME environment
  // variable does
  // not exist

  const auto home = Utility::getEnvironmentVariableOrDefault("HOME", "");
  const auto default_credentials_file_path = absl::StrCat(home, DEFAULT_AWS_CONFIG_FILE);

  return getEnvironmentVariableOrDefault(AWS_CONFIG_FILE, default_credentials_file_path);
}

std::string Utility::getCredentialProfileName() {
  return getEnvironmentVariableOrDefault(AWS_PROFILE, DEFAULT_AWS_PROFILE);
}

std::string Utility::getConfigProfileName() {
  auto profile_name = getEnvironmentVariableOrDefault(AWS_PROFILE, DEFAULT_AWS_PROFILE);
  if (profile_name == DEFAULT_AWS_PROFILE) {
    return profile_name;
  } else {
    return "profile " + profile_name;
  }
}

std::string Utility::getStringFromJsonOrDefault(Json::ObjectSharedPtr json_object,
                                                const std::string& string_value,
                                                const std::string& string_default) {
  absl::StatusOr<Envoy::Json::ValueType> value_or_error;
  value_or_error = json_object->getValue(string_value);
  if ((!value_or_error.ok()) || (!absl::holds_alternative<std::string>(value_or_error.value()))) {

    ENVOY_LOG_MISC(error, "Unable to retrieve string value from json: {}", string_value);
    return string_default;
  }
  return absl::get<std::string>(value_or_error.value());
}

int64_t Utility::getIntegerFromJsonOrDefault(Json::ObjectSharedPtr json_object,
                                             const std::string& integer_value,
                                             const int64_t integer_default) {
  absl::StatusOr<Envoy::Json::ValueType> value_or_error;
  value_or_error = json_object->getValue(integer_value);
  if (!value_or_error.ok() || ((!absl::holds_alternative<double>(value_or_error.value())) &&
                               (!absl::holds_alternative<int64_t>(value_or_error.value())))) {
    ENVOY_LOG_MISC(error, "Unable to retrieve integer value from json: {}", integer_value);
    return integer_default;
  }
  auto json_integer = value_or_error.value();
  // Handle double formatted integers IE exponent format such as 1.714449238E9
  if (auto* double_integer = absl::get_if<double>(&json_integer)) {
    if (*double_integer < 0) {
      ENVOY_LOG_MISC(error, "Integer {} less than 0: {}", integer_value, *double_integer);
      return integer_default;
    } else {
      return int64_t(*double_integer);
    }
  } else {
    // Standard integer
    return absl::get<int64_t>(json_integer);
  }
}

bool Utility::useDoubleUriEncode(const std::string service_name) {
  // These services require unmodified (not normalized) URL paths
  // https://github.com/boto/botocore/blob/66d047b5cdb033e4406e306afc5ab1c3e4785f16/botocore/data/s3control/2018-08-20/endpoint-rule-set-1.json#L368
  // https://github.com/boto/botocore/blob/66d047b5cdb033e4406e306afc5ab1c3e4785f16/botocore/data/s3/2006-03-01/endpoint-rule-set-1.json#L4347
  // https://github.com/boto/botocore/blob/66d047b5cdb033e4406e306afc5ab1c3e4785f16/botocore/data/s3/2006-03-01/endpoint-rule-set-1.json#L390

  constexpr absl::string_view S3_SERVICE_NAME = "s3";
  constexpr absl::string_view S3_OUTPOSTS_SERVICE_NAME = "s3-outposts";
  constexpr absl::string_view S3_EXPRESS_SERVICE_NAME = "s3-express";

  const std::vector<absl::string_view> do_not_normalize = {
      S3_SERVICE_NAME, S3_OUTPOSTS_SERVICE_NAME, S3_EXPRESS_SERVICE_NAME};

  for (auto& it : do_not_normalize) {
    if (absl::EqualsIgnoreCase(service_name, it)) {
      return false;
    }
  }

  return true;
}

// Even though these are two separate flags in the AWS SDKs, they are used in conjunction with each
// other
bool Utility::shouldNormalizeUriPath(const std::string service) {
  return Utility::useDoubleUriEncode(service);
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
